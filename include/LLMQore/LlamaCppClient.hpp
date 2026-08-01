// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QJsonObject>
#include <QUrl>

#include <LLMQore/OpenAIClient.hpp>

namespace LLMQore {

class LLMQORE_EXPORT LlamaCppClient : public OpenAIClient
{
    Q_OBJECT
public:
    explicit LlamaCppClient(QObject *parent = nullptr);
    explicit LlamaCppClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);
    explicit LlamaCppClient(
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

    QFuture<bool> isServerReady();
    QFuture<QJsonObject> serverProps();

protected:
    void processBufferedResponse(const RequestID &id, const QByteArray &data) override;
    void processStreamEvent(const RequestID &id, const QJsonObject &chunk) override;
    [[nodiscard]] const QLoggingCategory &logCategory() const override;

private:
    // llama.cpp's own /completion shape: bare text, no `choices` envelope.
    static bool isNativeCompletionChunk(const QJsonObject &chunk);
    void applyNativeUsage(const RequestID &id, const QJsonObject &chunk);
};

} // namespace LLMQore
