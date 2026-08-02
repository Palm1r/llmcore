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

class QLoggingCategory;

namespace LLMQore::Rpc {

// The part of a JSON-RPC protocol that MCP and ACP do the same way: it owns the
// session, follows the transport's life, answers `ping`, refuses requests that
// are sent before the handshake, reports a failed handshake, and warns when the
// negotiated version is one this build has never heard of.
//
// A protocol keeps what makes it a protocol -- its methods, its parameter types
// and its capabilities -- and hands the rest here.
class LLMQORE_EXPORT ProtocolPeer : public QObject
{
    Q_OBJECT

public:
    explicit ProtocolPeer(Transport *transport, QObject *parent = nullptr);
    ~ProtocolPeer() override;

    Transport *transport() const { return m_transport.data(); }
    JsonRpcSession *session() const { return m_session; }

    bool isInitialized() const { return m_initialized; }

    // Opens the transport if it is not already open.
    void open();
    void close();

    // The handshake itself: it bypasses the gate, opens the peer on success and
    // announces the failure on `errorOccurred` before handing it to the caller.
    QFuture<QJsonValue> handshake(
        const QString &method,
        const QJsonObject &params,
        std::chrono::milliseconds timeout);

    // Every other request: refused with a ProtocolError until the handshake has
    // landed, so a caller that skipped it fails the same way in both protocols.
    QFuture<QJsonValue> request(
        const QString &method,
        const QJsonObject &params = {},
        std::chrono::milliseconds timeout = std::chrono::seconds(30));

    // A request that is legal before the handshake (ping, cancellation).
    QFuture<QJsonValue> requestUngated(
        const QString &method,
        const QJsonObject &params = {},
        std::chrono::milliseconds timeout = std::chrono::seconds(30));

    void notify(const QString &method, const QJsonObject &params = {});

    // Installs a handler that refuses with MethodNotFound while `available`
    // answers false -- the refusal names the capability instead of repeating a
    // literal at every call site. `call` returns a future of void, of a type
    // with `toJson()`, or of QJsonValue.
    template<typename Available, typename Call>
    void bindRequest(
        const QString &method, const QString &capability, Available available, Call call);

    void bindNotification(
        const QString &method, std::function<void(const QJsonObject &)> handler);

    // A peer that names a version this build predates is not an error: the
    // protocol says so, and rejecting it would break against live agents.
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
