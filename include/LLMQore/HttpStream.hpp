// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>

#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/HttpTransportError.hpp>
#include <LLMQore/LLMQore_global.h>

QT_FORWARD_DECLARE_CLASS(QNetworkReply)

namespace LLMQore {

class HttpClient;

class LLMQORE_EXPORT HttpStream : public HttpStreamHandle
{
    Q_OBJECT
public:
    ~HttpStream() override;

    [[nodiscard]] int statusCode() const override;
    [[nodiscard]] QList<QPair<QByteArray, QByteArray>> rawHeaders() const override;
    [[nodiscard]] QByteArray rawHeader(QByteArrayView name) const;
    [[nodiscard]] QString contentType() const;

    [[nodiscard]] bool isFinished() const noexcept;

    void abort() override;

private:
    friend class HttpClient;
    explicit HttpStream(QNetworkReply *reply, QObject *parent = nullptr);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LLMQore
