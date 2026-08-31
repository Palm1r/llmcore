// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <functional>
#include <type_traits>

#include <QFuture>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/JsonRpcSession.hpp>
#include <LLMQore/LLMQore_global.h>
#include <LLMQore/RpcExceptions.hpp>
#include <LLMQore/RpcTransport.hpp>

QT_FORWARD_DECLARE_CLASS(QLoggingCategory)

namespace LLMQore::Rpc {

class LLMQORE_EXPORT ProtocolPeer : public QObject
{
    Q_OBJECT

public:
    explicit ProtocolPeer(Transport *transport, QObject *parent = nullptr);
    ~ProtocolPeer() override;

    Transport *transport() const { return m_transport.data(); }
    JsonRpcSession *session() const { return m_session; }

    bool isInitialized() const { return m_initialized; }

    void open();
    void close();

    QFuture<QJsonValue> handshake(
        const QString &method,
        const QJsonObject &params,
        std::chrono::milliseconds timeout);

    QFuture<QJsonValue> request(
        const QString &method,
        const QJsonObject &params = {},
        std::chrono::milliseconds timeout = std::chrono::seconds(30));

    QFuture<QJsonValue> requestUngated(
        const QString &method,
        const QJsonObject &params = {},
        std::chrono::milliseconds timeout = std::chrono::seconds(30));

    void notify(const QString &method, const QJsonObject &params = {});

    template<typename Available, typename Call>
    void bindRequest(
        const QString &method, const QString &capability, Available available, Call call);

    void bindNotification(
        const QString &method, std::function<void(const QJsonObject &)> handler);

    void warnOnUnknownVersion(
        const QString &negotiated, const QStringList &known, const QLoggingCategory &log);

signals:
    void errorOccurred(const QString &error);
    void closed();

private:
    template<typename T>
    QFuture<QJsonValue> asJsonFuture(QFuture<T> future);

    QPointer<Transport> m_transport;
    JsonRpcSession *m_session = nullptr;
    bool m_initialized = false;
};

template<typename T>
QFuture<QJsonValue> ProtocolPeer::asJsonFuture(QFuture<T> future)
{
    if constexpr (std::is_same_v<T, QJsonValue>) {
        return future;
    } else if constexpr (std::is_void_v<T>) {
        return LLMQore::compat(future).then(this, []() { return QJsonValue(QJsonObject{}); });
    } else {
        return LLMQore::compat(future).then(
            this, [](const T &result) { return QJsonValue(result.toJson()); });
    }
}

template<typename Available, typename Call>
void ProtocolPeer::bindRequest(
    const QString &method, const QString &capability, Available available, Call call)
{
    m_session->setRequestHandler(
        method,
        [this, capability, available, call](const QJsonObject &params) -> QFuture<QJsonValue> {
            if (!available()) {
                return LLMQore::failedFuture<QJsonValue>(RemoteError(
                    ErrorCode::MethodNotFound,
                    QStringLiteral("%1 not supported").arg(capability)));
            }
            return asJsonFuture(call(params));
        });
}

} // namespace LLMQore::Rpc
