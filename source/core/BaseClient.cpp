// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QThread>
#include <QUrlQuery>
#include <QUuid>

#include "Usage.hpp"
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpClient.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/HttpTransportError.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/ToolsManager.hpp>

namespace LLMQore {

namespace {

struct DataBuffers
{
    Rpc::LineFramer lineFramer;
    SSEParser sseParser;
    QString responseContent;

    void clear()
    {
        lineFramer.clear();
        sseParser.clear();
        responseContent.clear();
    }
};

struct ActiveRequest
{
    QPointer<HttpStreamHandle> stream;

    bool errorMode = false;
    QByteArray errorBody = {};

    DataBuffers buffers = {};

    QUrl url = {};
    QJsonObject originalPayload = {};
    QJsonObject finalPayload = {};
    RequestMode mode = RequestMode::Streaming;
    QString stopReason = {};
    std::optional<TokenUsage> usage = {};
    std::optional<TokenUsage> turnUsage = {};
    int toolRounds = 0;

    QPointer<BaseMessage> message;
};

} // namespace

struct BaseClient::Impl
{
    HttpTransport *transport = nullptr;
    AuthScheme authScheme;
    QHash<QString, QString> headers;
    ToolsManager *toolsManager = nullptr;
    int maxToolRounds = BaseClient::kDefaultMaxToolRounds;
    const QLoggingCategory *logCategory = &llmQoreLog();
    QHash<RequestID, ActiveRequest> requests;
};

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
    , m_impl(std::make_unique<Impl>())
{
    m_impl->transport = transport ? transport : new HttpClient(this);
}

BaseClient::~BaseClient()
{
    for (auto it = m_impl->requests.begin(); it != m_impl->requests.end(); ++it) {
        if (!it->stream)
            continue;
        it->stream->disconnect();
        it->stream->abort();
        delete it->stream;
    }
    m_impl->requests.clear();
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
    return m_impl->authScheme;
}

void BaseClient::setAuthScheme(const AuthScheme &scheme)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::setAuthScheme called from non-owning thread");
    m_impl->authScheme = scheme;
}

QHash<QString, QString> BaseClient::headers() const
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::headers called from non-owning thread");
    return m_impl->headers;
}

void BaseClient::setHeader(const QString &name, const QString &value)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::setHeader called from non-owning thread");
    m_impl->headers.insert(name, value);
}

void BaseClient::setHeaders(const QHash<QString, QString> &headers)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::setHeaders called from non-owning thread");
    m_impl->headers = headers;
}

QNetworkRequest BaseClient::prepareNetworkRequest(const QUrl &url) const
{
    QNetworkRequest request(url);

    for (auto it = m_impl->headers.cbegin(); it != m_impl->headers.cend(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());

    if (m_apiKey.isEmpty() || m_impl->authScheme.name.isEmpty())
        return request;

    switch (m_impl->authScheme.placement) {
    case AuthScheme::Placement::Header:
        request.setRawHeader(
            m_impl->authScheme.name.toUtf8(), (m_impl->authScheme.valuePrefix + m_apiKey).toUtf8());
        break;
    case AuthScheme::Placement::QueryParam: {
        QUrl requestUrl = request.url();
        QUrlQuery query(requestUrl.query());
        query.addQueryItem(m_impl->authScheme.name, m_impl->authScheme.valuePrefix + m_apiKey);
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
    return m_impl->transport;
}

int BaseClient::transferTimeoutMs() const
{
    return m_impl->transport->transferTimeoutMs();
}

void BaseClient::setTransferTimeout(int milliseconds)
{
    m_impl->transport->setTransferTimeout(milliseconds);
}

ToolsManager *BaseClient::tools()
{
    if (!m_impl->toolsManager) {
        m_impl->toolsManager = new ToolsManager(toolDialect(), this);

        connect(
            m_impl->toolsManager, &ToolsManager::toolExecutionStarted,
            this, &BaseClient::toolStarted);
        connect(
            m_impl->toolsManager, &ToolsManager::toolExecutionResult,
            this, &BaseClient::toolResultReady);
        connect(
            m_impl->toolsManager,
            &ToolsManager::toolExecutionComplete,
            this,
            &BaseClient::handleToolsCompleted);
    }
    return m_impl->toolsManager;
}

bool BaseClient::hasTools() const noexcept
{
    return m_impl->toolsManager != nullptr;
}

int BaseClient::maxToolContinuations() const
{
    return m_impl->maxToolRounds;
}

void BaseClient::setMaxToolContinuations(int limit)
{
    m_impl->maxToolRounds = limit > 0 ? limit : 1;
}

int BaseClient::toolRounds(const RequestID &id) const
{
    auto it = m_impl->requests.constFind(id);
    return it == m_impl->requests.constEnd() ? 0 : it->toolRounds;
}

void BaseClient::handleToolsCompleted(
    const RequestID &id, const QHash<QString, ToolResult> &toolResults)
{
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return;

    if (++it->toolRounds > m_impl->maxToolRounds) {
        qCWarning(llmQoreLog).noquote()
            << QString("Tool continuation limit reached for request %1").arg(id);
        abortRequest(id, QStringLiteral("Tool continuation limit reached"));
        return;
    }

    const QJsonObject payload = buildReplayContinuation(id, toolResults);
    if (payload.isEmpty()) {
        qCWarning(llmQoreLog).noquote()
            << QString("Missing data for continuation request %1").arg(id);
        abortRequest(id, QStringLiteral("Missing data for tool continuation"));
        return;
    }

    continueRequest(id, payload);
}

RequestID BaseClient::createRequest()
{
    RequestID id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto registerRequest = [this, id]() { m_impl->requests[id] = ActiveRequest{}; };
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
    auto it = m_impl->requests.find(id);
    if (it != m_impl->requests.end())
        it->mode = mode;
    startHttpRequest(id, prepareNetworkRequest(url), payload, mode);
}

const QLoggingCategory &BaseClient::logCategory() const
{
    return *m_impl->logCategory;
}

void BaseClient::setLogCategory(const QLoggingCategory &category)
{
    m_impl->logCategory = &category;
}

void BaseClient::cleanupDerivedData(const RequestID &)
{}

BaseMessage *BaseClient::messageForRequest(const RequestID &id) const
{
    auto it = m_impl->requests.constFind(id);
    if (it == m_impl->requests.constEnd())
        return nullptr;
    return it->message.data();
}

void BaseClient::setMessageForRequest(const RequestID &id, BaseMessage *message)
{
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end()) {
        if (message)
            message->deleteLater();
        return;
    }

    if (it->message == message)
        return;

    if (it->message)
        it->message->deleteLater();
    it->message = message;
}

QUrl BaseClient::endpointUrl(const QString &endpoint, const QString &defaultPath) const
{
    return QUrl(m_url + (endpoint.isEmpty() ? defaultPath : endpoint));
}

QFuture<QList<QString>> BaseClient::fetchModelList(
    const QUrl &url,
    const QString &arrayKey,
    const QString &idKey,
    const std::function<QString(QString)> &idMapper)
{
    const QNetworkRequest request = prepareNetworkRequest(url);
    const QLoggingCategory *cat = &logCategory();

    return LLMQore::compat(transport()->send(request, QByteArrayView("GET")))
        .then(this, [cat, arrayKey, idKey, idMapper](const HttpResponse &response) {
            const QLoggingCategory &log = *cat;

            QList<QString> models;
            if (!response.isSuccess()) {
                qCDebug(log).noquote()
                    << QString("Error fetching models: HTTP %1").arg(response.statusCode);
                return models;
            }

            const QJsonObject json = QJsonDocument::fromJson(response.body).object();
            const QJsonArray entries = json.value(arrayKey).toArray();
            for (const QJsonValue &value : entries) {
                QString id = value.toObject().value(idKey).toString();
                if (id.isEmpty())
                    continue;
                if (idMapper)
                    id = idMapper(std::move(id));
                if (!id.isEmpty())
                    models.append(id);
            }
            return models;
        })
        .onFailed(this, [cat](const std::exception &e) {
            const QLoggingCategory &log = *cat;
            qCDebug(log).noquote() << QString("Error fetching models: %1").arg(e.what());
            return QList<QString>{};
        });
}

QString BaseClient::parseErrorObject(
    const HttpResponse &response, const QList<ErrorAnnotation> &annotations) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(response.body);
    if (!doc.isObject())
        return BaseClient::parseHttpError(response);

    const QJsonObject error = doc.object().value("error").toObject();
    const QString message = error.value("message").toString();
    if (message.isEmpty())
        return BaseClient::parseHttpError(response);

    QString out = QString("HTTP %1: %2").arg(response.statusCode).arg(message);
    for (const ErrorAnnotation &annotation : annotations) {
        const QJsonValue value = error.value(annotation.field);
        QString text;
        if (value.isString())
            text = value.toString();
        else if (value.isDouble() && value.toInt() != 0)
            text = QString::number(value.toInt());
        if (text.isEmpty())
            continue;

        out += annotation.label.isEmpty()
            ? QString(" (%1)").arg(text)
            : QString(" (%1: %2)").arg(annotation.label, text);
    }
    return out;
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
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return;

    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    if (mode == RequestMode::Buffered) {
        (void)LLMQore::compat(m_impl->transport->send(request, QByteArrayView("POST"), body))
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

    HttpStreamHandle *stream = m_impl->transport->openStream(request, QByteArrayView("POST"), body);

    it = m_impl->requests.find(id);
    if (it == m_impl->requests.end()) {
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
        auto it = m_impl->requests.find(id);
        if (it == m_impl->requests.end() || it->stream != guardedStream)
            return;
        const int status = guardedStream->statusCode();
        if (status < 200 || status >= 300)
            it->errorMode = true;
    });

    connect(stream, &HttpStreamHandle::chunkReceived, this, [this, id, guardedStream](const QByteArray &chunk) {
        if (!guardedStream)
            return;
        auto it = m_impl->requests.find(id);
        if (it == m_impl->requests.end() || it->stream != guardedStream)
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
        auto it = m_impl->requests.find(id);
        if (it == m_impl->requests.end() || it->stream != guardedStream) {
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
        auto it = m_impl->requests.find(id);
        if (it == m_impl->requests.end() || it->stream != guardedStream) {
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
    if (!error)
        error = takePendingStreamError(id);

    if (error) {
        cleanupFullRequest(id);
        failRequest(id, *error);
        return;
    }

    if (hasRequest(id))
        flushStreamBuffers(id);
    if (!hasRequest(id))
        return;

    onStreamDrained(id);
    if (!hasRequest(id))
        return;

    auto *msg = messageForRequest(id);
    if (msg && msg->state() == MessageState::RequiresToolExecution)
        return;

    captureStopReason(id);

    cleanupFullRequest(id);
    completeRequest(id);
}

void BaseClient::captureStopReason(const RequestID &id)
{
    auto *msg = messageForRequest(id);
    if (!msg)
        return;

    auto it = m_impl->requests.find(id);
    if (it != m_impl->requests.end())
        it->stopReason = msg->stopReason();
}

void BaseClient::addChunk(const RequestID &id, const QString &chunk)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::addChunk called from non-owning thread");
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return;

    it->buffers.responseContent += chunk;
    const QString accumulated = it->buffers.responseContent;

    emit chunkReceived(id, chunk);
    if (!m_impl->requests.contains(id))
        return;
    emit accumulatedReceived(id, accumulated);
}

void BaseClient::completeRequest(const RequestID &id)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::completeRequest called from non-owning thread");
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return;

    finalizeTurn(id);

    QString fullText = it->buffers.responseContent;
    QString stopReason = it->stopReason;
    std::optional<TokenUsage> usage = it->usage;
    QJsonObject requestPayload = it->finalPayload.isEmpty() ? it->originalPayload
                                                           : it->finalPayload;
    cleanupRequest(id);

    CompletionInfo info;
    info.fullText = fullText;
    info.model = m_model;
    info.stopReason = stopReason;
    info.usage = usage;
    info.requestPayload = requestPayload;
    emit requestFinalized(id, info);
    emit requestCompleted(id, fullText);
}

void BaseClient::setUsage(const RequestID &id, const TokenUsage &usage)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::setUsage called from non-owning thread");
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return;
    it->turnUsage = usage;
}

std::optional<TokenUsage> BaseClient::currentUsage(const RequestID &id) const
{
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return std::nullopt;
    return it->turnUsage;
}

void BaseClient::applyUsage(const RequestID &id, const QJsonObject &root)
{
    applyUsage(id, root, usageSchema());
}

void BaseClient::applyUsage(
    const RequestID &id, const QJsonObject &root, const UsageSchema &schema)
{
    const UsageDelta delta = parseUsage(root, schema);
    if (delta.isEmpty())
        return;

    setUsage(id, applyTo(delta, currentUsage(id).value_or(TokenUsage{})));
}

void BaseClient::finalizeTurn(const RequestID &id)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::finalizeTurn called from non-owning thread");
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end() || !it->turnUsage)
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
    if (!m_impl->requests.contains(id))
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

    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
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

    const auto toolUseContent = msg->getCurrentToolUseContent();
    if (toolUseContent.isEmpty())
        return;

    QList<ToolRound::Call> calls;
    calls.reserve(toolUseContent.size());
    for (const auto *toolContent : toolUseContent)
        calls.append(
            ToolRound::Call{toolContent->id(), toolContent->name(), toolContent->input()});

    tools()->beginRound(id, calls);
}

QJsonObject BaseClient::buildReplayContinuation(
    const RequestID &id, const QHash<QString, ToolResult> &toolResults)
{
    auto *message = messageForRequest(id);
    auto it = m_impl->requests.find(id);
    if (!message || it == m_impl->requests.end())
        return {};

    return buildContinuationPayload(it->originalPayload, message, toolResults);
}

void BaseClient::continueRequest(const RequestID &id, const QJsonObject &payload)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::continueRequest called from non-owning thread");
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end() || it->url.isEmpty()) {
        qCWarning(llmQoreLog).noquote()
            << QString("Missing transport context for continuation request %1").arg(id);
        cleanupFullRequest(id);
        failRequest(id, QStringLiteral("Missing data for tool continuation"));
        return;
    }

    finalizeTurn(id);
    sendRequest(id, it->url, payload, it->mode);
}

void BaseClient::cleanupFullRequest(const RequestID &id)
{
    cleanupDerivedData(id);

    auto it = m_impl->requests.find(id);
    if (it != m_impl->requests.end()) {
        if (it->message)
            it->message->deleteLater();
        it->message = nullptr;
        it->url.clear();
        it->finalPayload = it->originalPayload;
        it->originalPayload = {};
        it->mode = RequestMode::Streaming;
    }

    if (m_impl->toolsManager)
        m_impl->toolsManager->cleanupRequest(id);
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
    if (!message || !hasRequest(id))
        return;

    // Walks the blocks in wire order and announces each one once. Both thinking
    // shapes go out the same way, and the "already announced" mark lives on the
    // block, so a request that is torn down mid-loop cannot re-announce.
    for (ContentBlock *block : message->getCurrentBlocks()) {
        if (auto *thinking = dynamic_cast<ThinkingContent *>(block)) {
            if (thinking->isNotified() || thinking->thinking().trimmed().isEmpty())
                continue;
            thinking->markNotified();
            emit thinkingBlockReceived(id, thinking->thinking(), thinking->signature());
        } else if (auto *redacted = dynamic_cast<RedactedThinkingContent *>(block)) {
            if (redacted->isNotified())
                continue;
            redacted->markNotified();
            emit thinkingBlockReceived(id, QString(), redacted->signature());
        } else {
            continue;
        }

        if (!hasRequest(id))
            return;
    }
}

void BaseClient::flushStreamBuffers(const RequestID &id)
{
    dispatchSseEvents(id, requestSSEParser(id).flush());
}

std::optional<QString> BaseClient::takePendingStreamError(const RequestID &)
{
    return std::nullopt;
}

void BaseClient::onStreamDrained(const RequestID &)
{}

void BaseClient::processData(const RequestID &id, const QByteArray &data)
{
    if (!hasRequest(id))
        return;

    dispatchSseEvents(id, requestSSEParser(id).append(data));
}

void BaseClient::dispatchSseEvents(const RequestID &id, const QList<SSEEvent> &events)
{
    for (int i = 0; i < events.size(); ++i) {
        const SSEEvent &ev = events.at(i);
        if (ev.data.isEmpty() || ev.data == "[DONE]")
            continue;

        const QJsonObject json = QJsonDocument::fromJson(ev.data).object();
        if (json.isEmpty())
            continue;

        processSseEvent(id, ev, json);

        if (!hasRequest(id)) {
            const int dropped = events.size() - i - 1;
            if (dropped > 0) {
                qCDebug(logCategory()).noquote()
                    << QString("Dropped %1 event(s) after the request ended: %2")
                           .arg(dropped)
                           .arg(id);
            }
            return;
        }
    }
}

void BaseClient::processSseEvent(const RequestID &, const SSEEvent &, const QJsonObject &)
{}

void BaseClient::storeRequestContext(const RequestID &id, const QUrl &url, const QJsonObject &payload)
{
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return;

    it->url = url;
    it->originalPayload = payload;
    it->buffers.lineFramer.clear();
    it->buffers.sseParser.clear();
}

bool BaseClient::hasRequest(const RequestID &id) const noexcept
{
    return m_impl->requests.contains(id);
}

Rpc::LineFramer &BaseClient::requestLineFramer(const RequestID &id)
{
    auto it = m_impl->requests.find(id);
    Q_ASSERT(it != m_impl->requests.end());
    return it->buffers.lineFramer;
}

SSEParser &BaseClient::requestSSEParser(const RequestID &id)
{
    auto it = m_impl->requests.find(id);
    Q_ASSERT(it != m_impl->requests.end());
    return it->buffers.sseParser;
}

QString BaseClient::responseContent(const RequestID &id) const
{
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return {};
    return it->buffers.responseContent;
}

void BaseClient::setResponseContent(const RequestID &id, const QString &content)
{
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end() || it->buffers.responseContent == content)
        return;

    it->buffers.responseContent = content;
    emit accumulatedReceived(id, content);
}

void BaseClient::cleanupRequest(const RequestID &id)
{
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return;

    if (it->stream) {
        it->stream->disconnect();
        it->stream->abort();
        it->stream->deleteLater();
        it->stream = nullptr;
    }

    if (it->message)
        it->message->deleteLater();

    m_impl->requests.erase(it);
}

} // namespace LLMQore
