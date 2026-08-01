// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/MistralClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>

#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/Log.hpp>

namespace LLMQore {

MistralClient::MistralClient(QObject *parent)
    : MistralClient({}, {}, {}, parent)
{}

MistralClient::MistralClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : OpenAIClient(url, apiKey, model, parent)
{}

MistralClient::MistralClient(
    const QString &url,
    const QString &apiKey,
    const QString &model,
    HttpTransport *transport,
    QObject *parent)
    : OpenAIClient(url, apiKey, model, transport, parent)
{}

const QLoggingCategory &MistralClient::logCategory() const
{
    return llmMistralLog();
}

RequestID MistralClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    return OpenAIClient::sendMessage(
        payload, endpoint.isEmpty() ? QStringLiteral("/v1/chat/completions") : endpoint, mode);
}

QFuture<QList<QString>> MistralClient::listModels(const QString &endpoint)
{
    return OpenAIClient::listModels(
        endpoint.isEmpty() ? QStringLiteral("/v1/models") : endpoint);
}

} // namespace LLMQore
