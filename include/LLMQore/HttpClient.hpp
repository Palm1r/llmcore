// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <memory>

#include <QByteArray>
#include <QByteArrayView>
#include <QFuture>
#include <QObject>

#include <LLMQore/HttpResponse.hpp>
#include <LLMQore/HttpStream.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/HttpTransportError.hpp>
#include <LLMQore/LLMQore_global.h>

QT_FORWARD_DECLARE_CLASS(QNetworkProxy)
QT_FORWARD_DECLARE_CLASS(QNetworkRequest)

namespace LLMQore {

class LLMQORE_EXPORT HttpClient : public HttpTransport
{
    Q_OBJECT
public:
    explicit HttpClient(QObject *parent = nullptr);
    ~HttpClient() override;

    [[nodiscard]] QFuture<HttpResponse> send(
        const QNetworkRequest &request,
        QByteArrayView verb,
        const QByteArray &body = {}) override;

    [[nodiscard]] HttpStream *openStream(
        const QNetworkRequest &request,
        QByteArrayView verb,
        const QByteArray &body = {}) override;

    void setProxy(const QNetworkProxy &proxy);

    using HttpTransport::setTransferTimeout;
    void setTransferTimeout(std::chrono::milliseconds timeout);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LLMQore
