// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/ClaudeClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>

#include "ClaudeMessage.hpp"
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/SSEParser.hpp>

namespace LLMQore {

ClaudeClient::ClaudeClient(QObject *parent)
    : ClaudeClient({}, {}, {}, parent)
{}

ClaudeClient::ClaudeClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : ClaudeClient(url, apiKey, model, nullptr, parent)
{}

ClaudeClient::ClaudeClient(
    const QString &url,
    const QString &apiKey,
    const QString &model,
    HttpTransport *transport,
    QObject *parent)
    : BaseClient(url, apiKey, model, transport, parent)
{
    setLogCategory(llmClaudeLog());
    setAuthScheme({.placement = AuthScheme::Placement::Header, .name = QStringLiteral("x-api-key")});
    setHeaders(
        {{QStringLiteral("Content-Type"), QStringLiteral("application/json")},
         {QStringLiteral("anthropic-version"), QStringLiteral("2023-06-01")}});
}

RequestID ClaudeClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);

    RequestID id = createRequest();
    const QString resolved = endpoint.isEmpty() ? QStringLiteral("/v1/messages") : endpoint;

    qCDebug(llmClaudeLog).noquote() << QString("Sending request %1 to %2").arg(id, resolved);

    sendRequest(id, QUrl(m_url + resolved), request, mode);
    return id;
}

RequestID ClaudeClient::ask(const QString &prompt, RequestMode mode)
{
    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 4096;
    payload["messages"] = QJsonArray{QJsonObject{{"role", "user"}, {"content", prompt}}};

    return sendMessage(payload, {}, mode);
}

const ToolDialect &ClaudeClient::toolDialect() const
{
    return ClaudeMessage::toolDialect();
}

QFuture<QList<QString>> ClaudeClient::listModels(const QString &endpoint)
{
    QUrl url = endpointUrl(endpoint, QStringLiteral("/v1/models"));
    QUrlQuery query;
    query.addQueryItem("limit", "1000");
    url.setQuery(query);

    return fetchModelList(url);
}

QString ClaudeClient::parseHttpError(const HttpResponse &response) const
{
    return parseErrorObject(response, {{{}, QStringLiteral("type")}});
}

QJsonObject ClaudeClient::buildContinuationPayload(
    const QJsonObject &originalPayload,
    BaseMessage *message,
    const QHash<QString, ToolResult> &toolResults)
{
    auto *claudeMsg = qobject_cast<ClaudeMessage *>(message);
    if (!claudeMsg)
        return originalPayload;

    QJsonObject request = originalPayload;
    QJsonArray messages = request["messages"].toArray();

    messages.append(claudeMsg->toProviderFormat());

    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = claudeMsg->createToolResultsContent(toolResults);
    messages.append(userMessage);

    request["messages"] = messages;
    return request;
}

void ClaudeClient::processSseEvent(
    const RequestID &id, const SSEEvent &, const QJsonObject &event)
{
    QString eventType = event["type"].toString();

    if (eventType == "message_stop")
        return;

    ClaudeMessage *message = messageAs<ClaudeMessage>(id);
    if (!message) {
        if (eventType != "message_start") {
            qCWarning(llmClaudeLog).noquote()
                << QString("Dropping event '%1' for request %2: no active message (missing "
                           "message_start?)")
                       .arg(eventType, id);
            return;
        }
        message = ensureMessage<ClaudeMessage>(id);
        qCDebug(llmClaudeLog).noquote()
            << QString("Created ClaudeMessage for request %1").arg(id);
    }

    if (eventType == "message_start") {
        message->startNewContinuation();
        qCDebug(llmClaudeLog).noquote() << QString("Starting continuation for request %1").arg(id);

        const QJsonObject usage = event["message"].toObject().value("usage").toObject();
        if (!usage.isEmpty()) {
            TokenUsage u;
            u.promptTokens = usage.value("input_tokens").toInt();
            u.completionTokens = usage.value("output_tokens").toInt();
            u.cachedPromptTokens = usage.value("cache_read_input_tokens").toInt();
            setUsage(id, u);
        }

    } else if (eventType == "content_block_start") {
        int index = event["index"].toInt();
        QJsonObject contentBlock = event["content_block"].toObject();
        QString blockType = contentBlock["type"].toString();

        message->handleContentBlockStart(index, blockType, contentBlock);

    } else if (eventType == "content_block_delta") {
        int index = event["index"].toInt();
        QJsonObject delta = event["delta"].toObject();
        QString deltaType = delta["type"].toString();

        message->handleContentBlockDelta(index, deltaType, delta);

        if (deltaType == "text_delta") {
            QString text = delta["text"].toString();
            addChunk(id, text);
        }

    } else if (eventType == "content_block_stop") {
        int index = event["index"].toInt();

        notifyPendingThinkingBlocks(id);

        message->handleContentBlockStop(index);

    } else if (eventType == "message_delta") {
        QJsonObject delta = event["delta"].toObject();
        if (delta.contains("stop_reason")) {
            message->handleStopReason(delta["stop_reason"].toString());
            executeToolsFromMessage(id);
        }
        const QJsonObject usage = event.value("usage").toObject();
        if (!usage.isEmpty()) {
            TokenUsage u = currentUsage(id).value_or(TokenUsage{});
            if (usage.contains("output_tokens"))
                u.completionTokens = usage.value("output_tokens").toInt();
            if (usage.contains("input_tokens"))
                u.promptTokens = usage.value("input_tokens").toInt();
            if (usage.contains("cache_read_input_tokens"))
                u.cachedPromptTokens = usage.value("cache_read_input_tokens").toInt();
            setUsage(id, u);
        }
    }
}

void ClaudeClient::processBufferedResponse(const RequestID &id, const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        failRequest(id, QStringLiteral("Invalid JSON in buffered response"));
        return;
    }

    QJsonObject response = doc.object();

    if (response["error"].isObject()) {
        QJsonObject error = response["error"].toObject();
        failRequest(id, error["message"].toString());
        return;
    }

    auto *message = ensureMessage<ClaudeMessage>(id);
    message->startNewContinuation();

    QJsonArray content = response["content"].toArray();
    for (int i = 0; i < content.size(); ++i) {
        QJsonObject block = content[i].toObject();
        QString blockType = block["type"].toString();

        message->handleContentBlockStart(i, blockType, block);

        if (blockType == "text") {
            QString text = block["text"].toString();
            if (!text.isEmpty()) {
                message->handleContentBlockDelta(
                    i, QStringLiteral("text_delta"), QJsonObject{{"text", text}});
                addChunk(id, text);
            }
        } else if (blockType == "thinking") {
            QString thinking = block["thinking"].toString();
            if (!thinking.isEmpty()) {
                message->handleContentBlockDelta(
                    i, QStringLiteral("thinking_delta"), QJsonObject{{"thinking", thinking}});
            }
            if (block.contains("signature")) {
                message->handleContentBlockDelta(
                    i,
                    QStringLiteral("signature_delta"),
                    QJsonObject{{"signature", block["signature"].toString()}});
            }
            notifyPendingThinkingBlocks(id);
        } else if (blockType == "redacted_thinking") {
            notifyPendingThinkingBlocks(id);
        }

        message->handleContentBlockStop(i);
    }

    QString stopReason = response["stop_reason"].toString();
    if (!stopReason.isEmpty()) {
        message->handleStopReason(stopReason);
        executeToolsFromMessage(id);
    }

    const QJsonObject usage = response.value("usage").toObject();
    if (!usage.isEmpty()) {
        TokenUsage u;
        u.promptTokens = usage.value("input_tokens").toInt();
        u.completionTokens = usage.value("output_tokens").toInt();
        u.cachedPromptTokens = usage.value("cache_read_input_tokens").toInt();
        setUsage(id, u);
    }
}

} // namespace LLMQore
