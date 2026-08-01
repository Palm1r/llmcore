// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QUrl>

#include <QLoggingCategory>

#include <LLMQore/BaseClient.hpp>
#include <LLMQore/SSEParser.hpp>

namespace LLMQore {

class OpenAIMessage;

class LLMQORE_EXPORT OpenAIClient : public BaseClient
{
    Q_OBJECT
public:
    explicit OpenAIClient(QObject *parent = nullptr);
    explicit OpenAIClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);
    explicit OpenAIClient(
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
    void processBufferedResponse(const RequestID &id, const QByteArray &data) override;
    QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults) override;
    [[nodiscard]] QString parseHttpError(const HttpResponse &response) const override;

    void processSseEvent(
        const RequestID &id, const SSEEvent &event, const QJsonObject &json) override;

    // Reads `usage` off any response object -- streamed chunk or buffered body.
    void applyUsage(const RequestID &id, const QJsonObject &responseObject);

    // Which category the shared code logs under, so an OpenAI-compatible
    // provider still reports under its own name.
    [[nodiscard]] const QLoggingCategory &logCategory() const override;

private:
    static QString takeReasoningAndText(OpenAIMessage *message, const QJsonObject &source);
    void processStreamChunk(const RequestID &id, const QJsonObject &chunk);

};

} // namespace LLMQore
