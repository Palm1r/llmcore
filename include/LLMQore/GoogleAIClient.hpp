// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QUrl>

#include <LLMQore/BaseClient.hpp>

namespace LLMQore {

class GoogleMessage;

class LLMQORE_EXPORT GoogleAIClient : public BaseClient
{
    Q_OBJECT
public:
    explicit GoogleAIClient(QObject *parent = nullptr);
    explicit GoogleAIClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);
    explicit GoogleAIClient(
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
    ToolSchemaFormat toolSchemaFormat() const override { return ToolSchemaFormat::Google; }

    QFuture<QList<QString>> listModels(const QString &endpoint = {}) override;

protected:
    void processData(const RequestID &id, const QByteArray &data) override;
    void processBufferedResponse(const RequestID &id, const QByteArray &data) override;
    void onStreamFinished(const RequestID &id, std::optional<QString> error) override;
    void cleanupDerivedData(const RequestID &id) override;
    QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults) override;
    [[nodiscard]] QString parseHttpError(const HttpResponse &response) const override;
    [[nodiscard]] const QLoggingCategory &logCategory() const override;

private:
    // A 200-with-error-body response is not SSE-framed, so it has to be
    // reassembled across chunks before it can be recognised.
    class JsonErrorSniffer
    {
    public:
        static constexpr qsizetype kMaxBytes = 64 * 1024;

        std::optional<QString> append(const QByteArray &chunk);

    private:
        bool m_active = true;
        QByteArray m_buffer;
    };

    void processStreamChunk(const RequestID &id, const QJsonObject &chunk);

    QHash<RequestID, QString> m_failedRequests;
    QHash<RequestID, JsonErrorSniffer> m_errorSniffers;
};

} // namespace LLMQore
