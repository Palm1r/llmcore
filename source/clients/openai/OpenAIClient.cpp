// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/OpenAIClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include "OpenAIMessage.hpp"
#include "OpenAIUsage.hpp"
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/SSEParser.hpp>

namespace LLMQore {

OpenAIClient::OpenAIClient(QObject *parent)
    : OpenAIClient({}, {}, {}, parent)
{}

OpenAIClient::OpenAIClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : OpenAIClient(url, apiKey, model, nullptr, parent)
{}

OpenAIClient::OpenAIClient(
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

const QLoggingCategory &OpenAIClient::logCategory() const
{
    return llmOpenAILog();
}

RequestID OpenAIClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);

    if (mode == RequestMode::Streaming) {
        QJsonObject streamOptions = request.value("stream_options").toObject();
        streamOptions["include_usage"] = true;
        request["stream_options"] = streamOptions;
    }

    RequestID id = createRequest();
    const QString resolved = endpoint.isEmpty() ? QStringLiteral("/chat/completions") : endpoint;

    qCDebug(logCategory()).noquote() << QString("Sending request %1 to %2").arg(id, resolved);

    sendRequest(id, QUrl(m_url + resolved), request, mode);
    return id;
}

RequestID OpenAIClient::ask(const QString &prompt, RequestMode mode)
{
    QJsonObject payload;
    payload["model"] = m_model;
    payload["messages"] = QJsonArray{QJsonObject{{"role", "user"}, {"content", prompt}}};

    return sendMessage(payload, {}, mode);
}

QFuture<QList<QString>> OpenAIClient::listModels(const QString &endpoint)
{
    return fetchModelList(endpointUrl(endpoint, QStringLiteral("/models")));
}

QString OpenAIClient::parseHttpError(const HttpResponse &response) const
{
    return parseErrorObject(
        response,
        {{QStringLiteral("type"), QStringLiteral("type")},
         {QStringLiteral("code"), QStringLiteral("code")}});
}

void OpenAIClient::processData(const RequestID &id, const QByteArray &data)
{
    if (!hasRequest(id))
        return;

    dispatchStreamEvents(id, requestSSEParser(id).append(data));
}

void OpenAIClient::dispatchStreamEvents(const RequestID &id, const QList<SSEEvent> &events)
{
    for (int i = 0; i < events.size(); ++i) {
        const SSEEvent &ev = events.at(i);
        if (ev.data.isEmpty() || ev.data == "[DONE]")
            continue;
        const QJsonObject chunk = QJsonDocument::fromJson(ev.data).object();
        if (chunk.isEmpty())
            continue;

        processStreamEvent(id, chunk);

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

void OpenAIClient::processStreamEvent(const RequestID &id, const QJsonObject &chunk)
{
    if (chunk.contains("choices"))
        processStreamChunk(id, chunk);

    const QJsonObject usage = chunk.value("usage").toObject();
    if (!usage.isEmpty())
        setUsage(id, parseOpenAIUsage(usage));
}

QJsonObject OpenAIClient::buildContinuationPayload(
    const QJsonObject &originalPayload,
    BaseMessage *message,
    const QHash<QString, ToolResult> &toolResults)
{
    auto *openaiMsg = qobject_cast<OpenAIMessage *>(message);
    if (!openaiMsg)
        return originalPayload;

    return appendChatMessagesContinuation(
        originalPayload,
        openaiMsg->toProviderFormat(),
        openaiMsg->createToolResultMessages(toolResults));
}

void OpenAIClient::processStreamChunk(const RequestID &id, const QJsonObject &chunk)
{
    QJsonArray choices = chunk["choices"].toArray();
    if (choices.isEmpty())
        return;

    QJsonObject choice = choices[0].toObject();
    QJsonObject delta = choice["delta"].toObject();
    QString finishReason = choice["finish_reason"].toString();

    OpenAIMessage *message = ensureMessage<OpenAIMessage>(id);

    // DeepSeek-style reasoning: dedicated "reasoning_content" field
    if (delta.contains("reasoning_content") && !delta["reasoning_content"].isNull())
        message->handleReasoningDelta(delta["reasoning_content"].toString());

    // Standard text (string) or Mistral Magistral thinking+text chunks (array)
    if (delta.contains("content") && !delta["content"].isNull()) {
        const OpenAIMessage::ContentParts parts
            = OpenAIMessage::splitContentParts(delta["content"]);
        if (!parts.thinking.isEmpty())
            message->handleReasoningDelta(parts.thinking);
        if (!parts.text.isEmpty()) {
            notifyPendingThinkingBlocks(id);
            message->handleContentDelta(parts.text);
            addChunk(id, parts.text);
        }
    }

    if (delta.contains("tool_calls")) {
        QJsonArray toolCalls = delta["tool_calls"].toArray();
        for (const auto &toolCallValue : toolCalls) {
            QJsonObject toolCall = toolCallValue.toObject();
            int index = toolCall["index"].toInt();
            QJsonObject function = toolCall["function"].toObject();

            if (toolCall.contains("id"))
                message->handleToolCallStart(index, toolCall["id"].toString(),
                                             function["name"].toString());

            if (function.contains("arguments"))
                message->handleToolCallDelta(index, function["arguments"].toString());
        }
    }

    if (!finishReason.isEmpty() && finishReason != "null") {
        notifyPendingThinkingBlocks(id);
        message->completeAllPendingToolCalls();
        message->handleFinishReason(finishReason);
        executeToolsFromMessage(id);
    }
}

void OpenAIClient::processBufferedResponse(const RequestID &id, const QByteArray &data)
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

    QJsonArray choices = response["choices"].toArray();
    if (choices.isEmpty()) {
        failRequest(id, QStringLiteral("Empty choices in buffered response"));
        return;
    }

    QJsonObject choice = choices[0].toObject();
    QJsonObject messageObj = choice["message"].toObject();
    QString finishReason = choice["finish_reason"].toString();

    auto *message = ensureMessage<OpenAIMessage>(id);

    // DeepSeek-style reasoning: dedicated "reasoning_content" field
    if (messageObj.contains("reasoning_content") && !messageObj["reasoning_content"].isNull())
        message->handleReasoningDelta(messageObj["reasoning_content"].toString());

    // Standard text (string) or Mistral Magistral thinking+text chunks (array)
    if (messageObj.contains("content") && !messageObj["content"].isNull()) {
        const OpenAIMessage::ContentParts parts
            = OpenAIMessage::splitContentParts(messageObj["content"]);
        if (!parts.thinking.isEmpty())
            message->handleReasoningDelta(parts.thinking);
        if (!parts.text.isEmpty()) {
            message->handleContentDelta(parts.text);
            addChunk(id, parts.text);
        }
    }

    notifyPendingThinkingBlocks(id);

    if (messageObj.contains("tool_calls")) {
        QJsonArray toolCalls = messageObj["tool_calls"].toArray();
        for (int i = 0; i < toolCalls.size(); ++i) {
            QJsonObject toolCall = toolCalls[i].toObject();
            QString toolId = toolCall["id"].toString();
            QJsonObject function = toolCall["function"].toObject();
            QString name = function["name"].toString();
            QString arguments = function["arguments"].toString();

            message->handleToolCallStart(i, toolId, name);
            message->handleToolCallDelta(i, arguments);
            message->handleToolCallComplete(i);
        }
    }

    if (!finishReason.isEmpty()) {
        message->handleFinishReason(finishReason);
        executeToolsFromMessage(id);
    }

    const QJsonObject usage = response.value("usage").toObject();
    if (!usage.isEmpty())
        setUsage(id, parseOpenAIUsage(usage));
}

} // namespace LLMQore
