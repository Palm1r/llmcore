// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/OllamaClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>

#include "OllamaMessage.hpp"
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/RpcLineFramer.hpp>
#include <LLMQore/Log.hpp>

namespace LLMQore {

OllamaClient::OllamaClient(QObject *parent)
    : OllamaClient({}, {}, {}, parent)
{}

OllamaClient::OllamaClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : OllamaClient(url, apiKey, model, nullptr, parent)
{}

OllamaClient::OllamaClient(
    const QString &url,
    const QString &apiKey,
    const QString &model,
    HttpTransport *transport,
    QObject *parent)
    : BaseClient(url, apiKey, model, transport, parent)
{
    setAuthScheme(
        {.placement = AuthScheme::Placement::Header,
         .name = QStringLiteral("Authorization"),
         .valuePrefix = QStringLiteral("Bearer ")});
    setHeaders({{QStringLiteral("Content-Type"), QStringLiteral("application/json")}});
}

RequestID OllamaClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);

    RequestID id = createRequest();
    const QString resolved = endpoint.isEmpty() ? QStringLiteral("/api/chat") : endpoint;

    qCDebug(llmOllamaLog).noquote() << QString("Sending request %1 to %2").arg(id, resolved);

    sendRequest(id, QUrl(m_url + resolved), request, mode);
    return id;
}

RequestID OllamaClient::ask(const QString &prompt, RequestMode mode)
{
    QJsonObject payload;
    payload["model"] = m_model;
    payload["messages"] = QJsonArray{QJsonObject{{"role", "user"}, {"content", prompt}}};

    return sendMessage(payload, {}, mode);
}

const ToolDialect &OllamaClient::toolDialect() const
{
    return OllamaMessage::toolDialect();
}

const QLoggingCategory &OllamaClient::logCategory() const
{
    return llmOllamaLog();
}

QFuture<QList<QString>> OllamaClient::listModels(const QString &endpoint)
{
    return fetchModelList(
        endpointUrl(endpoint, QStringLiteral("/api/tags")),
        QStringLiteral("models"),
        QStringLiteral("name"));
}

QString OllamaClient::parseHttpError(const HttpResponse &response) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(response.body);
    if (doc.isObject()) {
        const QString message = doc.object().value("error").toString();
        if (!message.isEmpty())
            return QString("HTTP %1: %2").arg(response.statusCode).arg(message);
    }
    return BaseClient::parseHttpError(response);
}

void OllamaClient::processData(const RequestID &id, const QByteArray &data)
{
    if (data.isEmpty())
        return;

    if (!hasRequest(id))
        return;

    const QByteArrayList lines = requestLineFramer(id).append(data);

    for (const QByteArray &line : lines) {
        if (line.trimmed().isEmpty())
            continue;

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (doc.isNull() || !doc.isObject()) {
            qCDebug(llmOllamaLog).noquote()
                << QString("Failed to parse JSON: %1").arg(error.errorString());
            continue;
        }

        QJsonObject obj = doc.object();

        if (obj.contains("error") && !obj["error"].toString().isEmpty()) {
            QString errorMsg = obj["error"].toString();
            qCWarning(llmOllamaLog).noquote() << "Error in response: " + errorMsg;
            cleanupFullRequest(id);
            failRequest(id, errorMsg);
            return;
        }

        processStreamData(id, obj);
    }
}

QJsonObject OllamaClient::buildContinuationPayload(
    const QJsonObject &originalPayload,
    BaseMessage *message,
    const QHash<QString, ToolResult> &toolResults)
{
    auto *ollamaMsg = qobject_cast<OllamaMessage *>(message);
    if (!ollamaMsg)
        return originalPayload;

    return appendChatMessagesContinuation(
        originalPayload,
        ollamaMsg->toProviderFormat(),
        ollamaMsg->createToolResultMessages(toolResults));
}

void OllamaClient::onStreamFinished(const RequestID &id, std::optional<QString> error)
{
    if (!error && hasRequest(id)) {
        Rpc::LineFramer &framer = requestLineFramer(id);
        if (framer.hasIncompleteData()) {
            const QByteArray remaining = framer.currentBuffer().trimmed();
            framer.clear();

            if (!remaining.isEmpty()) {
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(remaining, &parseError);
                if (!doc.isNull() && doc.isObject()) {
                    QJsonObject obj = doc.object();
                    if (obj.contains("error") && !obj["error"].toString().isEmpty()) {
                        QString errorMsg = obj["error"].toString();
                        qCWarning(llmOllamaLog).noquote()
                            << "Error in remaining buffer: " + errorMsg;
                        cleanupFullRequest(id);
                        failRequest(id, errorMsg);
                        return;
                    }
                    processStreamData(id, obj);
                }
            }
        }
    }

    BaseClient::onStreamFinished(id, error);
}

void OllamaClient::processStreamData(const RequestID &id, const QJsonObject &data)
{
    OllamaMessage *message = ensureMessage<OllamaMessage>(id);

    if (data.contains("thinking")) {
        QString thinkingDelta = data["thinking"].toString();
        if (!thinkingDelta.isEmpty())
            message->handleThinkingDelta(thinkingDelta);
    }

    if (data.contains("message")) {
        QJsonObject messageObj = data["message"].toObject();

        if (messageObj.contains("thinking")) {
            QString thinkingDelta = messageObj["thinking"].toString();
            if (!thinkingDelta.isEmpty())
                message->handleThinkingDelta(thinkingDelta);
        }

        if (messageObj.contains("content")) {
            QString content = messageObj["content"].toString();
            if (!content.isEmpty()) {
                notifyPendingThinkingBlocks(id);
                message->handleContentDelta(content);
                if (!message->isAccumulatingToolCall())
                    addChunk(id, content);
            }
        }

        if (messageObj.contains("tool_calls")) {
            QJsonArray toolCalls = messageObj["tool_calls"].toArray();
            for (const auto &toolCallValue : toolCalls)
                message->handleToolCall(toolCallValue.toObject());
        }
    } else if (data.contains("response")) {
        QString content = data["response"].toString();
        if (!content.isEmpty()) {
            message->handleContentDelta(content);
            addChunk(id, content);
        }
    }

    if (data["done"].toBool()) {
        if (data.contains("signature")) {
            message->handleThinkingComplete(data["signature"].toString());
        }

        message->handleDone(true, data.value("done_reason").toString());

        if (data.contains("prompt_eval_count") || data.contains("eval_count")) {
            TokenUsage u;
            u.promptTokens = data.value("prompt_eval_count").toInt();
            u.completionTokens = data.value("eval_count").toInt();
            setUsage(id, u);
        }

        notifyPendingThinkingBlocks(id);
        executeToolsFromMessage(id);
    }
}

void OllamaClient::processBufferedResponse(const RequestID &id, const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        failRequest(id, QStringLiteral("Invalid JSON in buffered response"));
        return;
    }

    QJsonObject response = doc.object();

    if (response.contains("error") && !response["error"].toString().isEmpty()) {
        failRequest(id, response["error"].toString());
        return;
    }

    processStreamData(id, response);
}

} // namespace LLMQore
