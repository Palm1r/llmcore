// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/McpClient.hpp>

#include <LLMQore/BaseClient.hpp>
#include <LLMQore/BaseElicitationProvider.hpp>
#include <LLMQore/BaseRootsProvider.hpp>
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/RpcExceptions.hpp>
#include <LLMQore/JsonRpcSession.hpp>
#include <LLMQore/RpcTransport.hpp>

#include <QJsonArray>
#include <QPromise>

namespace LLMQore::Mcp {

McpClient::McpClient(Rpc::Transport *transport, Implementation clientInfo, QObject *parent)
    : QObject(parent)
    , m_transport(transport)
    , m_session(new Rpc::JsonRpcSession(transport, this))
    , m_clientInfo(std::move(clientInfo))
{
    if (m_transport) {
        connect(
            m_transport,
            &Rpc::Transport::closed,
            this,
            [this]() {
                m_initialized = false;
                emit disconnected();
            });
        connect(
            m_transport,
            &Rpc::Transport::errorOccurred,
            this,
            &McpClient::errorOccurred);
    }

    installHandlers();
}

McpClient::~McpClient() = default;

void McpClient::installHandlers()
{
    m_session->setNotificationHandler(
        QLatin1String(Method::ToolsListChanged),
        [this](const QJsonObject &) {
            m_cachedTools.clear();
            emit toolsChanged();
        });
    m_session->setNotificationHandler(
        QLatin1String(Method::ResourcesListChanged),
        [this](const QJsonObject &) { emit resourcesChanged(); });
    m_session->setNotificationHandler(
        QLatin1String(Method::ResourcesUpdated),
        [this](const QJsonObject &params) {
            emit resourceUpdated(params.value("uri").toString());
        });
    m_session->setNotificationHandler(
        QLatin1String(Method::PromptsListChanged),
        [this](const QJsonObject &) { emit promptsChanged(); });
    m_session->setNotificationHandler(
        QLatin1String(Method::LoggingMessage), [this](const QJsonObject &params) {
            emit logMessage(
                params.value("level").toString(),
                params.value("logger").toString(),
                params.value("data"),
                params.value("message").toString());
        });

    m_session->setRequestHandler(
        QLatin1String(Method::RootsList),
        [this](const QJsonObject &) -> QFuture<QJsonValue> {
            if (!m_rootsProvider)
                return LLMQore::readyFuture<QJsonValue>(QJsonObject{{"roots", QJsonArray{}}});

            return LLMQore::compat(m_rootsProvider->listRoots())
                .then(this, [](const QList<Root> &list) {
                    QJsonArray arr;
                    for (const Root &r : list)
                        arr.append(r.toJson());
                    return QJsonValue(QJsonObject{{"roots", arr}});
                })
                .onFailed(this, [](const std::exception &) {
                    return QJsonValue(QJsonObject{{"roots", QJsonArray{}}});
                });
        });

    m_session->setRequestHandler(
        QLatin1String(Method::Ping), [](const QJsonObject &) -> QFuture<QJsonValue> {
            return LLMQore::readyFuture<QJsonValue>(QJsonObject{});
        });

    m_session->setRequestHandler(
        QLatin1String(Method::SamplingCreateMessage),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            if (!m_samplingClient || !m_samplingBuilder) {
                throw Rpc::RemoteError(
                    Rpc::ErrorCode::MethodNotFound,
                    QStringLiteral("sampling/createMessage not supported"));
            }

            const CreateMessageParams req = CreateMessageParams::fromJson(params);
            QJsonObject payload;
            try {
                payload = m_samplingBuilder(req);
            } catch (const std::exception &e) {
                throw Rpc::RemoteError(
                    Rpc::ErrorCode::InvalidParams,
                    QString::fromUtf8(e.what()));
            }

            auto promise = std::make_shared<QPromise<QJsonValue>>();
            promise->start();

            // The sampling client is expected to live on the same thread as
            // the MCP client — otherwise the ordering guarantee below (that
            // no signals can fire between connect() and sendMessage()) no
            // longer holds. Enforce it explicitly.
            Q_ASSERT_X(
                m_samplingClient->thread() == this->thread(),
                "McpClient::sampling",
                "samplingClient must live on the same thread as McpClient");

            // Bridge the request-scoped response to the QPromise via Qt signals.
            // We connect to requestFinalized / requestFailed, filter by the
            // exact RequestID we get back from sendMessage, and self-disconnect
            // both handlers on the first firing so the promise resolves exactly
            // once. A shared "done" flag guards against the (theoretically
            // impossible but cheap to defend) double-resolve if the backend
            // ever emits both finalized and failed for the same request.
            struct BridgeState
            {
                LLMQore::RequestID id;
                QMetaObject::Connection finalizedConn;
                QMetaObject::Connection failedConn;
                bool done = false;
            };
            auto state = std::make_shared<BridgeState>();

            auto disconnectBoth = [state]() {
                QObject::disconnect(state->finalizedConn);
                QObject::disconnect(state->failedConn);
            };

            // Register the signal handlers BEFORE calling sendMessage() so
            // that a synchronously-delivered signal (possible in edge cases
            // like cached/stubbed clients) cannot fire into the void. The
            // lambda body filters by state->id, which is empty until we
            // populate it right after sendMessage() returns — any signal
            // for an unrelated request won't match and is dropped.
            state->finalizedConn = QObject::connect(
                m_samplingClient, &LLMQore::BaseClient::requestFinalized, this,
                [state, promise, disconnectBoth](
                    const LLMQore::RequestID &id, const LLMQore::CompletionInfo &info) {
                    if (state->done || state->id.isEmpty() || id != state->id)
                        return;
                    state->done = true;
                    disconnectBoth();
                    CreateMessageResult r;
                    r.role = QStringLiteral("assistant");
                    r.content = QJsonObject{
                        {"type", "text"},
                        {"text", info.fullText},
                    };
                    r.model = info.model;
                    r.stopReason = info.stopReason;
                    promise->addResult(QJsonValue(r.toJson()));
                    promise->finish();
                });

            state->failedConn = QObject::connect(
                m_samplingClient, &LLMQore::BaseClient::requestFailed, this,
                [state, promise, disconnectBoth](
                    const LLMQore::RequestID &id, const QString &err) {
                    if (state->done || state->id.isEmpty() || id != state->id)
                        return;
                    state->done = true;
                    disconnectBoth();
                    promise->setException(std::make_exception_ptr(
                        Rpc::RemoteError(Rpc::ErrorCode::InternalError, err)));
                    promise->finish();
                });

            state->id = m_samplingClient->sendMessage(payload);

            return promise->future();
        });

    m_session->setRequestHandler(
        QLatin1String(Method::ElicitationCreate),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            if (!m_elicitationProvider) {
                throw Rpc::RemoteError(
                    Rpc::ErrorCode::MethodNotFound,
                    QStringLiteral("elicitation/create not supported"));
            }
            const ElicitRequestParams req = ElicitRequestParams::fromJson(params);
            return LLMQore::compat(m_elicitationProvider->elicit(req))
                .then(this, [](const ElicitResult &result) { return QJsonValue(result.toJson()); });
        });
}

QFuture<InitializeResult> McpClient::connectAndInitialize(std::chrono::milliseconds timeout)
{
    if (!m_transport) {
        return LLMQore::compat(LLMQore::failedFuture<QJsonValue>(Rpc::TransportError(QStringLiteral("No transport"))))
            .then(this, [](const QJsonValue &) { return InitializeResult{}; });
    }

    if (!m_transport->isOpen())
        m_transport->start();

    ClientCapabilities clientCaps;
    if (m_rootsProvider)
        clientCaps.roots = RootsCapability{/*listChanged*/ true};
    if (m_samplingClient && m_samplingBuilder)
        clientCaps.sampling = SamplingCapability{true};
    if (m_elicitationProvider)
        clientCaps.elicitation = ElicitationCapability{true};

    const QJsonObject params{
        {"protocolVersion", QString::fromLatin1(kSupportedProtocolVersion)},
        {"capabilities", clientCaps.toJson()},
        {"clientInfo", m_clientInfo.toJson()},
    };

    return LLMQore::compat(m_session->sendRequest(QLatin1String(Method::Initialize), params, timeout))
        .then(this, [this](const QJsonValue &result) {
            m_initResult = InitializeResult::fromJson(result.toObject());
            m_initialized = true;

            const QString v = m_initResult.protocolVersion;
            bool known = false;
            for (const char *knownV : kKnownProtocolVersions) {
                if (v == QLatin1String(knownV)) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                qCWarning(llmMcpLog).noquote()
                    << QString("Unexpected protocol version from server: %1").arg(v);
            }

            m_session->sendNotification(QLatin1String(Method::Initialized));
            emit initialized(m_initResult);
            return m_initResult;
        })
        .onFailed(this, [this](const auto &e) -> InitializeResult {
            if constexpr (std::is_same_v<std::decay_t<decltype(e)>, Rpc::JsonRpcException>) {
                emit errorOccurred(e.message());
                throw Rpc::JsonRpcException(e.message());
            } else {
                const QString msg = QString::fromUtf8(e.what());
                emit errorOccurred(msg);
                throw Rpc::JsonRpcException(msg);
            }
        });
}

QFuture<QJsonValue> McpClient::sendInitialized(
    const QString &method, const QJsonObject &params)
{
    if (!m_initialized)
        return LLMQore::failedFuture<QJsonValue>(Rpc::ProtocolError(QStringLiteral("Client not initialized")));
    return m_session->sendRequest(method, params);
}

QFuture<void> McpClient::ping(std::chrono::milliseconds timeout)
{
    QFuture<QJsonValue> raw = (!m_transport || !m_transport->isOpen())
        ? LLMQore::failedFuture<QJsonValue>(Rpc::TransportError(QStringLiteral("Transport is not open")))
        : m_session->sendRequest(QLatin1String(Method::Ping), QJsonObject{}, timeout);
    return LLMQore::compat(raw).then(this, [](const QJsonValue &) {});
}

QFuture<void> McpClient::setLogLevel(const QString &level)
{
    return LLMQore::compat(
               sendInitialized(QLatin1String(Method::LoggingSetLevel), QJsonObject{{"level", level}}))
        .then(this, [](const QJsonValue &) {});
}

QFuture<QList<ToolInfo>> McpClient::listTools()
{
    return LLMQore::compat(sendInitialized(QLatin1String(Method::ToolsList)))
        .then(this, [this](const QJsonValue &result) {
            QList<ToolInfo> tools;
            const QJsonArray arr = result.toObject().value("tools").toArray();
            for (const QJsonValue &item : arr)
                tools.append(ToolInfo::fromJson(item.toObject()));
            m_cachedTools = tools;
            return tools;
        });
}

QFuture<LLMQore::ToolResult> McpClient::callTool(
    const QString &name, const QJsonObject &arguments)
{
    return LLMQore::compat(
               sendInitialized(QLatin1String(Method::ToolsCall), QJsonObject{{"name", name}, {"arguments", arguments}}))
        .then(this, [](const QJsonValue &result) {
            return LLMQore::ToolResult::fromJson(result.toObject());
        });
}

McpClient::CancellableToolCall McpClient::callToolWithProgress(
    const QString &name, const QJsonObject &arguments, ProgressCallback onProgress)
{
    CancellableToolCall out;

    if (!m_initialized) {
        out.future = LLMQore::failedFuture<LLMQore::ToolResult>(
            Rpc::ProtocolError(QStringLiteral("Client not initialized")));
        return out;
    }

    QJsonObject params{{"name", name}, {"arguments", arguments}};
    auto cancellable
        = m_session->sendCancellableRequest(QLatin1String(Method::ToolsCall), params);
    out.requestId = cancellable.requestId;
    out.progressToken = cancellable.requestId;

    if (onProgress) {
        m_session->setProgressHandler(
            cancellable.requestId,
            [onProgress](double progress, double total, const QString &message) {
                onProgress(progress, total, message);
            });
    }

    out.future = LLMQore::compat(cancellable.future)
                     .then(this, [](const QJsonValue &result) {
                         return LLMQore::ToolResult::fromJson(result.toObject());
                     });
    return out;
}

void McpClient::cancel(const QString &requestId, const QString &reason)
{
    if (m_session)
        m_session->cancelRequest(requestId, reason);
}

QFuture<QList<ResourceInfo>> McpClient::listResources()
{
    return LLMQore::compat(sendInitialized(QLatin1String(Method::ResourcesList)))
        .then(this, [](const QJsonValue &result) {
            QList<ResourceInfo> resources;
            const QJsonArray arr = result.toObject().value("resources").toArray();
            for (const QJsonValue &item : arr)
                resources.append(ResourceInfo::fromJson(item.toObject()));
            return resources;
        });
}

QFuture<QList<ResourceTemplate>> McpClient::listResourceTemplates()
{
    return LLMQore::compat(sendInitialized(QLatin1String(Method::ResourcesTemplatesList)))
        .then(this, [](const QJsonValue &result) {
            QList<ResourceTemplate> templates;
            const QJsonArray arr = result.toObject().value("resourceTemplates").toArray();
            for (const QJsonValue &item : arr)
                templates.append(ResourceTemplate::fromJson(item.toObject()));
            return templates;
        });
}

QFuture<ResourceContents> McpClient::readResource(const QString &uri)
{
    return LLMQore::compat(sendInitialized(QLatin1String(Method::ResourcesRead), QJsonObject{{"uri", uri}}))
        .then(this, [](const QJsonValue &result) {
            const QJsonObject obj = result.toObject();
            const QJsonArray contents = obj.value("contents").toArray();
            if (contents.isEmpty())
                return ResourceContents{};
            return ResourceContents::fromJson(contents.first().toObject());
        });
}

QFuture<void> McpClient::subscribeResource(const QString &uri)
{
    return LLMQore::compat(sendInitialized(QLatin1String(Method::ResourcesSubscribe), QJsonObject{{"uri", uri}}))
        .then(this, [](const QJsonValue &) {});
}

QFuture<void> McpClient::unsubscribeResource(const QString &uri)
{
    return LLMQore::compat(sendInitialized(QLatin1String(Method::ResourcesUnsubscribe), QJsonObject{{"uri", uri}}))
        .then(this, [](const QJsonValue &) {});
}

QFuture<QList<PromptInfo>> McpClient::listPrompts()
{
    return LLMQore::compat(sendInitialized(QLatin1String(Method::PromptsList)))
        .then(this, [](const QJsonValue &result) {
            QList<PromptInfo> prompts;
            const QJsonArray arr = result.toObject().value("prompts").toArray();
            for (const QJsonValue &item : arr)
                prompts.append(PromptInfo::fromJson(item.toObject()));
            return prompts;
        });
}

QFuture<PromptGetResult> McpClient::getPrompt(
    const QString &name, const QJsonObject &arguments)
{
    QJsonObject params{{"name", name}};
    if (!arguments.isEmpty())
        params.insert("arguments", arguments);

    return LLMQore::compat(sendInitialized(QLatin1String(Method::PromptsGet), params))
        .then(this, [](const QJsonValue &result) { return PromptGetResult::fromJson(result.toObject()); });
}

QFuture<CompletionResult> McpClient::complete(
    const CompletionReference &ref,
    const QString &argumentName,
    const QString &partialValue,
    const QJsonObject &contextArguments)
{
    QJsonObject params{
        {"ref", ref.toJson()},
        {"argument", QJsonObject{{"name", argumentName}, {"value", partialValue}}},
    };
    if (!contextArguments.isEmpty())
        params.insert("context", QJsonObject{{"arguments", contextArguments}});

    return LLMQore::compat(sendInitialized(QLatin1String(Method::CompletionComplete), params))
        .then(this, [](const QJsonValue &result) { return CompletionResult::fromJson(result.toObject()); });
}

void McpClient::setSamplingClient(
    LLMQore::BaseClient *client, SamplingPayloadBuilder builder)
{
    m_samplingClient = client;
    m_samplingBuilder = std::move(builder);
}

void McpClient::setElicitationProvider(BaseElicitationProvider *provider)
{
    m_elicitationProvider = provider;
}

void McpClient::setRootsProvider(BaseRootsProvider *provider)
{
    if (m_rootsProvider) {
        disconnect(m_rootsProvider, nullptr, this, nullptr);
    }
    m_rootsProvider = provider;
    if (provider) {
        connect(provider, &BaseRootsProvider::listChanged, this, [this]() {
            if (m_initialized)
                m_session->sendNotification(
                    QLatin1String(Method::RootsListChanged));
        });
    }
}

void McpClient::shutdown()
{
    if (m_transport && m_transport->isOpen())
        m_transport->stop();
    m_initialized = false;
}

} // namespace LLMQore::Mcp
