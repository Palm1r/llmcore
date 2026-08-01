// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QPair>

#include <LLMQore/HttpResponse.hpp>
#include <LLMQore/HttpTransportError.hpp>
#include <LLMQore/LLMQore_global.h>

class QNetworkRequest;

namespace LLMQore {

class LLMQORE_EXPORT HttpStreamHandle : public QObject
{
    Q_OBJECT
public:
    explicit HttpStreamHandle(QObject *parent = nullptr);
    ~HttpStreamHandle() override;

    [[nodiscard]] virtual int statusCode() const = 0;
    [[nodiscard]] virtual QList<QPair<QByteArray, QByteArray>> rawHeaders() const = 0;

    virtual void abort() = 0;

signals:
    void headersReceived();
    void chunkReceived(QByteArray chunk);
    void finished();
    void errorOccurred(LLMQore::HttpTransportError error);
};

class LLMQORE_EXPORT HttpTransport : public QObject
{
    Q_OBJECT
public:
    static constexpr int DefaultTransferTimeoutMs = 120000;

    explicit HttpTransport(QObject *parent = nullptr);
    ~HttpTransport() override;

    [[nodiscard]] virtual QFuture<HttpResponse> send(
        const QNetworkRequest &request,
        QByteArrayView verb,
        const QByteArray &body = {})
        = 0;

    [[nodiscard]] virtual HttpStreamHandle *openStream(
        const QNetworkRequest &request,
        QByteArrayView verb,
        const QByteArray &body = {})
        = 0;

    virtual void setTransferTimeout(int milliseconds);

    [[nodiscard]] virtual int transferTimeoutMs() const noexcept;

private:
    int m_transferTimeoutMs = DefaultTransferTimeoutMs;
};

} // namespace LLMQore
