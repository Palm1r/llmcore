// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseClient.hpp>

#include <QJsonDocument>
#include <QPointer>
#include <QThread>
#include <QUrlQuery>
#include <QUuid>

#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpClient.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/HttpTransportError.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/ToolLoopRunner.hpp>
#include <LLMQore/ToolsManager.hpp>

namespace LLMQore {

namespace {
constexpr qsizetype kMaxErrorBodyBytes = 64 * 1024;
} // namespace

BaseClient::BaseClient(QObject *parent)
    : LLMQore::BaseClient({}, {}, {}, parent)
{}

BaseClient::BaseClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : LLMQore::BaseClient(url, apiKey, model, nullptr, parent)
{}

BaseClient::BaseClient(
    const QString &url,
    const QString &apiKey,
    const QString &model,
    HttpTransport *transport,
    QObject *parent)
    : QObject(parent)
    , m_url(url)
    , m_apiKey(apiKey)
    , m_model(model)
    , m_transport(transport ? transport : new HttpClient(this))
{}

BaseClient::~BaseClient()
{
    for (auto it = m_requests.begin(); it != m_requests.end(); ++it) {
        if (!it->stream)
            continue;
        it->stream->disconnect();
        it->stream->abort();
        delete it->stream;
    }
    m_requests.clear();
}

QString BaseClient::url() const
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::url called from non-owning thread");
    return m_url;
}

void BaseClient::setUrl(const QString &url)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::setUrl called from non-owning thread");
    m_url = url;
}

QString BaseClient::apiKey() const
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::apiKey called from non-owning thread");
    return m_apiKey;
}

void BaseClient::setApiKey(const QString &apiKey)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::setApiKey called from non-owning thread");
    m_apiKey = apiKey;
}

QString BaseClient::model() const
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::model called from non-owning thread");
    return m_model;
}

void BaseClient::setModel(const QString &model)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::setModel called from non-owning thread");
    m_model = model;
}

AuthScheme BaseClient::authScheme() const
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::authScheme called from non-owning thread");
    return m_authScheme;
}

void BaseClient::setAuthScheme(const AuthScheme &scheme)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::setAuthScheme called from non-owning thread");
    m_authScheme = scheme;
}

QHash<QString, QString> BaseClient::headers() const
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::headers called from non-owning thread");
    return m_headers;
}

void BaseClient::setHeader(const QString &name, const QString &value)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::setHeader called from non-owning thread");
    m_headers.insert(name, value);
}

void BaseClient::setHeaders(const QHash<QString, QString> &headers)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::setHeaders called from non-owning thread");
    m_headers = headers;
}

QNetworkRequest BaseClient::prepareNetworkRequest(const QUrl &url) const
{
    QNetworkRequest request(url);

    for (auto it = m_headers.cbegin(); it != m_headers.cend(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());

    if (m_apiKey.isEmpty() || m_authScheme.name.isEmpty())
        return request;

    switch (m_authScheme.placement) {
    case AuthScheme::Placement::Header:
        request.setRawHeader(
            m_authScheme.name.toUtf8(), (m_authScheme.valuePrefix + m_apiKey).toUtf8());
        break;
    case AuthScheme::Placement::QueryParam: {
        QUrl requestUrl = request.url();
        QUrlQuery query(requestUrl.query());
        query.addQueryItem(m_authScheme.name, m_authScheme.valuePrefix + m_apiKey);
        requestUrl.setQuery(query);
        request.setUrl(requestUrl);
        break;
    }
    case AuthScheme::Placement::None:
        break;
    }

    return request;
}

HttpTransport *BaseClient::transport() const
{
    return m_transport;
}

int BaseClient::transferTimeoutMs() const
{
    return m_transport->transferTimeoutMs();
}

void BaseClient::setTransferTimeout(int milliseconds)
{
    m_transport->setTransferTimeout(milliseconds);
}

ToolsManager *BaseClient::tools()
{
    if (!m_toolsManager) {
        m_toolsManager = new ToolsManager(toolSchemaFormat(), this);

        connect(
            m_toolsManager, &ToolsManager::toolExecutionStarted,
            this, &BaseClient::toolStarted);
        connect(
            m_toolsManager, &ToolsManager::toolExecutionResult,
            this, &BaseClient::toolResultReady);
        connect(
            m_toolsManager,
            &ToolsManager::toolExecutionComplete,
            toolLoop(),
            &ToolLoopRunner::handleToolsCompleted);
    }
    return m_toolsManager;
}

bool BaseClient::hasTools() const noexcept
{
    return m_toolsManager != nullptr;
}

int BaseClient::maxToolContinuations() const
{
    return m_toolLoop ? m_toolLoop->maxRounds() : ToolLoopRunner::kDefaultMaxRounds;
}

void BaseClient::setMaxToolContinuations(int limit)
{
    toolLoop()->setMaxRounds(limit);
}

ToolLoopRunner *BaseClient::toolLoop()
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::toolLoop called from non-owning thread");
    if (!m_toolLoop)
        m_toolLoop = new ToolLoopRunner(this);
    return m_toolLoop;
}

RequestID BaseClient::createRequest()
{
    RequestID id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto registerRequest = [this, id]() { m_requests[id] = ActiveRequest{}; };
    if (thread() == QThread::currentThread())
        registerRequest();
    else
        QMetaObject::invokeMethod(this, registerRequest, Qt::QueuedConnection);
    return id;
}

void BaseClient::sendRequest(
    const RequestID &id, const QUrl &url, const QJsonObject &payload, RequestMode mode)
{
    if (thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(
            this,
            [this, id, url, payload, mode]() { sendRequest(id, url, payload, mode); },
            Qt::QueuedConnection);
        return;
    }
    storeRequestContext(id, url, payload);
    auto it = m_requests.find(id);
    if (it != m_requests.end())
        it->mode = mode;
    startHttpRequest(id, prepareNetworkRequest(url), payload, mode);
}

QString BaseClient::parseHttpError(const HttpResponse &response) const
{
    constexpr int kSnippetCap = 512;
    if (response.body.isEmpty())
        return QString("HTTP %1").arg(response.statusCode);
    const QString snippet = QString::fromUtf8(response.body.left(kSnippetCap));
    return QString("HTTP %1: %2").arg(response.statusCode).arg(snippet);
}

void BaseClient::startHttpRequest(
    const RequestID &id,
    const QNetworkRequest &request,
    const QJsonObject &payload,
    RequestMode mode)
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;

    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    if (mode == RequestMode::Buffered) {
        (void)LLMQore::compat(m_transport->send(request, QByteArrayView("POST"), body))
            .then(this, [this, id](const HttpResponse &response) {
                if (!hasRequest(id))
                    return;
                if (!response.isSuccess()) {
                    QString msg = parseHttpError(response);
                    if (msg.isEmpty())
                        msg = QString("HTTP %1").arg(response.statusCode);
                    onStreamFinished(id, msg);
                    return;
                }
                processBufferedResponse(id, response.body);
                onStreamFinished(id, std::nullopt);
            })
            .onFailed(this, [this, id](const auto &e) {
                if (!hasRequest(id))
                    return;
                if constexpr (std::is_same_v<std::decay_t<decltype(e)>, HttpTransportError>)
                    onStreamFinished(id, e.message());
                else
                    onStreamFinished(id, QString::fromUtf8(e.what()));
            });
        return;
    }

    HttpStreamHandle *stream = m_transport->openStream(request, QByteArrayView("POST"), body);

    it = m_requests.find(id);
    if (it == m_requests.end()) {
        delete stream;
        return;
    }
    if (!stream) {
        onStreamFinished(id, QStringLiteral("Transport returned no stream"));
        return;
    }

    it->stream = stream;
    it->errorMode = false;
    it->errorBody.clear();

    QPointer<HttpStreamHandle> guardedStream(stream);

    connect(stream, &HttpStreamHandle::headersReceived, this, [this, id, guardedStream]() {
        if (!guardedStream)
            return;
        auto it = m_requests.find(id);
        if (it == m_requests.end() || it->stream != guardedStream)
            return;
        const int status = guardedStream->statusCode();
        if (status < 200 || status >= 300)
            it->errorMode = true;
    });

    connect(stream, &HttpStreamHandle::chunkReceived, this, [this, id, guardedStream](const QByteArray &chunk) {
        if (!guardedStream)
            return;
        auto it = m_requests.find(id);
        if (it == m_requests.end() || it->stream != guardedStream)
            return;
        if (it->errorMode) {
            const qsizetype room = kMaxErrorBodyBytes - it->errorBody.size();
            if (room > 0)
                it->errorBody.append(chunk.left(room));
            return;
        }
        processData(id, chunk);
    });

    connect(stream, &HttpStreamHandle::finished, this, [this, id, guardedStream]() {
        if (!guardedStream) {
            return;
        }
        auto it = m_requests.find(id);
        if (it == m_requests.end() || it->stream != guardedStream) {
            guardedStream->deleteLater();
            return;
        }

        std::optional<QString> error;
        if (it->errorMode) {
            HttpResponse r;
            r.statusCode = guardedStream->statusCode();
            r.rawHeaders = guardedStream->rawHeaders();
            r.body = it->errorBody;
            QString msg = parseHttpError(r);
            if (msg.isEmpty())
                msg = QString("HTTP %1").arg(r.statusCode);
            error = msg;
        }

        it->stream = nullptr;
        it->errorMode = false;
        it->errorBody.clear();
        guardedStream->disconnect();
        guardedStream->deleteLater();

        onStreamFinished(id, error);
    });

    connect(stream, &HttpStreamHandle::errorOccurred, this,
            [this, id, guardedStream](const HttpTransportError &e) {
        if (!guardedStream)
            return;
        auto it = m_requests.find(id);
        if (it == m_requests.end() || it->stream != guardedStream) {
            guardedStream->deleteLater();
            return;
        }
        it->stream = nullptr;
        it->errorMode = false;
        it->errorBody.clear();
        guardedStream->disconnect();
        guardedStream->deleteLater();
        onStreamFinished(id, e.message());
    });
}

void BaseClient::onStreamFinished(const RequestID &id, std::optional<QString> error)
{
    if (error) {
        cleanupFullRequest(id);
        failRequest(id, *error);
        return;
    }

    auto *msg = messageForRequest(id);
    if (msg && msg->state() == MessageState::RequiresToolExecution)
        return;

    if (msg) {
        auto it = m_requests.find(id);
        if (it != m_requests.end())
            it->stopReason = msg->stopReason();
    }

    cleanupFullRequest(id);
    completeRequest(id);
}

void BaseClient::addChunk(const RequestID &id, const QString &chunk)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::addChunk called from non-owning thread");
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;

    it->buffers.responseContent += chunk;
    const QString accumulated = it->buffers.responseContent;

    emit chunkReceived(id, chunk);
    if (!m_requests.contains(id))
        return;
    emit accumulatedReceived(id, accumulated);
}

void BaseClient::completeRequest(const RequestID &id)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::completeRequest called from non-owning thread");
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;

    finalizeTurn(id);

    QString fullText = it->buffers.responseContent;
    QString stopReason = it->stopReason;
    std::optional<TokenUsage> usage = it->usage;
    cleanupRequest(id);

    CompletionInfo info;
    info.fullText = fullText;
    info.model = m_model;
    info.stopReason = stopReason;
    info.usage = usage;
    emit requestFinalized(id, info);
    emit requestCompleted(id, fullText);
}

void BaseClient::setUsage(const RequestID &id, const TokenUsage &usage)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::setUsage called from non-owning thread");
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    it->turnUsage = usage;
}

std::optional<TokenUsage> BaseClient::currentUsage(const RequestID &id) const
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return std::nullopt;
    return it->turnUsage;
}

std::optional<TokenUsage> BaseClient::totalUsage(const RequestID &id) const
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return std::nullopt;

    if (!it->turnUsage)
        return it->usage;
    if (!it->usage)
        return it->turnUsage;

    TokenUsage combined = *it->usage;
    combined.promptTokens += it->turnUsage->promptTokens;
    combined.completionTokens += it->turnUsage->completionTokens;
    combined.cachedPromptTokens += it->turnUsage->cachedPromptTokens;
    combined.reasoningTokens += it->turnUsage->reasoningTokens;
    return combined;
}

void BaseClient::accumulateUsage(const RequestID &id, const TokenUsage &delta)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::accumulateUsage called from non-owning thread");
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    if (!it->turnUsage)
        it->turnUsage = TokenUsage{};
    it->turnUsage->promptTokens += delta.promptTokens;
    it->turnUsage->completionTokens += delta.completionTokens;
    it->turnUsage->cachedPromptTokens += delta.cachedPromptTokens;
    it->turnUsage->reasoningTokens += delta.reasoningTokens;
}

void BaseClient::finalizeTurn(const RequestID &id)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::finalizeTurn called from non-owning thread");
    auto it = m_requests.find(id);
    if (it == m_requests.end() || !it->turnUsage)
        return;

    if (!it->usage) {
        it->usage = it->turnUsage;
    } else {
        it->usage->promptTokens += it->turnUsage->promptTokens;
        it->usage->completionTokens += it->turnUsage->completionTokens;
        it->usage->cachedPromptTokens += it->turnUsage->cachedPromptTokens;
        it->usage->reasoningTokens += it->turnUsage->reasoningTokens;
    }
    it->turnUsage.reset();
}

void BaseClient::failRequest(const RequestID &id, const QString &error)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::failRequest called from non-owning thread");
    if (!m_requests.contains(id))
        return;

    cleanupRequest(id);
    emit requestFailed(id, error);
}

void BaseClient::cancelRequest(const RequestID &requestId)
{
    abortRequest(requestId, QStringLiteral("Request cancelled"));
}

void BaseClient::abortRequest(const RequestID &id, const QString &error)
{
    if (thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(
            this, [this, id, error]() { abortRequest(id, error); }, Qt::QueuedConnection);
        return;
    }

    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;

    if (it->stream) {
        it->stream->disconnect();
        it->stream->abort();
        it->stream->deleteLater();
        it->stream = nullptr;
    }

    cleanupFullRequest(id);
    failRequest(id, error);
}

void BaseClient::executeToolsFromMessage(const RequestID &id)
{
    auto *msg = messageForRequest(id);
    if (!msg)
        return;

    if (msg->state() != MessageState::RequiresToolExecution)
        return;

    auto toolUseContent = msg->getCurrentToolUseContent();
    if (toolUseContent.isEmpty())
        return;

    for (auto *toolContent : toolUseContent) {
        tools()->executeToolCall(id, toolContent->id(), toolContent->name(), toolContent->input());
    }
}

QJsonObject BaseClient::buildReplayContinuation(
    const RequestID &id, const QHash<QString, ToolResult> &toolResults)
{
    auto *message = messageForRequest(id);
    auto it = m_requests.find(id);
    if (!message || it == m_requests.end())
        return {};

    return buildContinuationPayload(it->originalPayload, message, toolResults);
}

void BaseClient::continueRequest(const RequestID &id, const QJsonObject &payload)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::continueRequest called from non-owning thread");
    auto it = m_requests.find(id);
    if (it == m_requests.end() || it->url.isEmpty()) {
        qCWarning(llmQoreLog).noquote()
            << QString("Missing transport context for continuation request %1").arg(id);
        cleanupFullRequest(id);
        failRequest(id, QStringLiteral("Missing data for tool continuation"));
        return;
    }

    it->emittedThinkingBlocksCount = 0;

    finalizeTurn(id);
    sendRequest(id, it->url, payload, it->mode);
}

void BaseClient::cleanupFullRequest(const RequestID &id)
{
    cleanupDerivedData(id);

    auto it = m_requests.find(id);
    if (it != m_requests.end()) {
        it->url.clear();
        it->originalPayload = {};
        it->emittedThinkingBlocksCount = 0;
        it->mode = RequestMode::Streaming;
    }

    if (m_toolsManager)
        m_toolsManager->cleanupRequest(id);
}

QJsonObject BaseClient::appendChatMessagesContinuation(
    const QJsonObject &originalPayload,
    const QJsonObject &assistantMessage,
    const QJsonArray &toolMessages)
{
    QJsonObject request = originalPayload;
    QJsonArray messages = request["messages"].toArray();

    messages.append(assistantMessage);
    for (const auto &toolMessage : toolMessages)
        messages.append(toolMessage);

    request["messages"] = messages;
    return request;
}

void BaseClient::notifyPendingThinkingBlocks(const RequestID &id)
{
    auto *message = messageForRequest(id);
    if (!message)
        return;

    auto thinkingBlocks = message->getCurrentThinkingContent();
    if (thinkingBlocks.isEmpty())
        return;

    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;

    const int alreadyEmitted = it->emittedThinkingBlocksCount;
    const int totalBlocks = thinkingBlocks.size();

    it->emittedThinkingBlocksCount = totalBlocks;

    for (int i = alreadyEmitted; i < totalBlocks; ++i) {
        auto *thinkingContent = thinkingBlocks[i];
        if (thinkingContent->thinking().trimmed().isEmpty())
            continue;
        emit thinkingBlockReceived(id, thinkingContent->thinking(), thinkingContent->signature());
        if (!m_requests.contains(id))
            return;
    }
}

void BaseClient::storeRequestContext(const RequestID &id, const QUrl &url, const QJsonObject &payload)
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;

    it->url = url;
    it->originalPayload = payload;
    it->buffers.lineFramer.clear();
    it->buffers.sseParser.clear();
}

bool BaseClient::hasRequest(const RequestID &id) const noexcept
{
    return m_requests.contains(id);
}

Rpc::LineFramer &BaseClient::requestLineFramer(const RequestID &id)
{
    auto it = m_requests.find(id);
    Q_ASSERT(it != m_requests.end());
    return it->buffers.lineFramer;
}

SSEParser &BaseClient::requestSSEParser(const RequestID &id)
{
    auto it = m_requests.find(id);
    Q_ASSERT(it != m_requests.end());
    return it->buffers.sseParser;
}

QString BaseClient::responseContent(const RequestID &id) const
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return {};
    return it->buffers.responseContent;
}

void BaseClient::setResponseContent(const RequestID &id, const QString &content)
{
    auto it = m_requests.find(id);
    if (it != m_requests.end())
        it->buffers.responseContent = content;
}

void BaseClient::cleanupRequest(const RequestID &id)
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;

    if (it->stream) {
        it->stream->disconnect();
        it->stream->abort();
        it->stream->deleteLater();
        it->stream = nullptr;
    }

    m_requests.erase(it);
}

} // namespace LLMQore
