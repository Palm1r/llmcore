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
    ToolSchemaFormat toolSchemaFormat() const override { return ToolSchemaFormat::OpenAI; }

    QFuture<QList<QString>> listModels(const QString &endpoint = {}) override;

protected:
    void processData(const RequestID &id, const QByteArray &data) override;
    void processBufferedResponse(const RequestID &id, const QByteArray &data) override;
    QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults) override;
    [[nodiscard]] QString parseHttpError(const HttpResponse &response) const override;

    // Runs already-framed SSE events through the dialect. Subclasses that need
    // the same dispatch from another point (a trailing flush, a second wire
    // shape) reuse this instead of re-implementing the loop.
    void dispatchStreamEvents(const RequestID &id, const QList<SSEEvent> &events);

    // One framed event. Override to recognise a provider-specific chunk shape,
    // and delegate here for anything OpenAI-compatible.
    virtual void processStreamEvent(const RequestID &id, const QJsonObject &chunk);

    // Which category the shared code logs under, so an OpenAI-compatible
    // provider still reports under its own name.
    [[nodiscard]] const QLoggingCategory &logCategory() const override;

private:
    void processStreamChunk(const RequestID &id, const QJsonObject &chunk);

};

} // namespace LLMQore
