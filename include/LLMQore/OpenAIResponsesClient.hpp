// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QUrl>

#include <LLMQore/BaseClient.hpp>
#include <LLMQore/SSEParser.hpp>

namespace LLMQore {

class OpenAIResponsesMessage;

class LLMQORE_EXPORT OpenAIResponsesClient : public BaseClient
{
    Q_OBJECT
public:
    explicit OpenAIResponsesClient(QObject *parent = nullptr);
    explicit OpenAIResponsesClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);
    explicit OpenAIResponsesClient(
        const QString &url,
        const QString &apiKey,
        const QString &model,
        HttpTransport *transport,
        QObject *parent = nullptr);

    RequestID sendMessage(
        const QJsonObject &payload,
        const QString &endpoint = {},
        RequestMode mode = RequestMode::Streaming) override;
    RequestID ask(
        const QString &prompt, RequestMode mode = RequestMode::Streaming) override;

    QFuture<QList<QString>> listModels(const QString &endpoint = {}) override;

protected:
    [[nodiscard]] const ToolDialect &toolDialect() const override;
    void processSseEvent(
        const RequestID &id, const SSEEvent &event, const QJsonObject &json) override;
    void processBufferedResponse(const RequestID &id, const QByteArray &data) override;
    void cleanupDerivedData(const RequestID &id) override;
    QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults) override;
    [[nodiscard]] QString parseHttpError(const HttpResponse &response) const override;

private:
    static QString extractAggregatedText(const QJsonObject &responseObj);
    static QString extractReasoningText(const QJsonObject &item);

    QHash<RequestID, QHash<QString, QString>> m_itemIdToCallId;
};

} // namespace LLMQore
