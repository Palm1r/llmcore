// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/ProtocolPeer.hpp>

#include <QLoggingCategory>

#include <LLMQore/Log.hpp>

namespace LLMQore::Rpc {

namespace {

constexpr const char *kPing = "ping";

} // namespace

ProtocolPeer::ProtocolPeer(Transport *transport, QObject *parent)
    : QObject(parent)
    , m_transport(transport)
    , m_session(new JsonRpcSession(transport, this))
{
    if (m_transport) {
        connect(m_transport, &Transport::closed, this, [this]() {
            m_initialized = false;
            emit closed();
        });
        connect(m_transport, &Transport::errorOccurred, this, &ProtocolPeer::errorOccurred);
    }

    m_session->setRequestHandler(
        QLatin1String(kPing), [](const QJsonObject &) -> QFuture<QJsonValue> {
            return LLMQore::readyFuture<QJsonValue>(QJsonObject{});
        });
}

ProtocolPeer::~ProtocolPeer() = default;

void ProtocolPeer::open()
{
    if (m_transport && !m_transport->isOpen())
        m_transport->start();
}

void ProtocolPeer::close()
{
    if (m_transport && m_transport->isOpen())
        m_transport->stop();
}

QFuture<QJsonValue> ProtocolPeer::handshake(
    const QString &method, const QJsonObject &params, std::chrono::milliseconds timeout)
{
    if (!m_transport)
        return LLMQore::failedFuture<QJsonValue>(TransportError(QStringLiteral("No transport")));

    open();

    return LLMQore::compat(m_session->sendRequest(method, params, timeout))
        .then(
            this,
            [this](const QJsonValue &result) {
                m_initialized = true;
                return result;
            })
        .onFailed(this, [this](const std::exception &e) -> QJsonValue {
            const QString message = QString::fromUtf8(e.what());
            emit errorOccurred(message);
            throw JsonRpcException(message);
        });
}

QFuture<QJsonValue> ProtocolPeer::request(
    const QString &method, const QJsonObject &params, std::chrono::milliseconds timeout)
{
    if (!m_initialized) {
        return LLMQore::failedFuture<QJsonValue>(
            ProtocolError(QStringLiteral("Client not initialized")));
    }
    return m_session->sendRequest(method, params, timeout);
}

QFuture<QJsonValue> ProtocolPeer::requestUngated(
    const QString &method, const QJsonObject &params, std::chrono::milliseconds timeout)
{
    if (!m_transport || !m_transport->isOpen()) {
        return LLMQore::failedFuture<QJsonValue>(
            TransportError(QStringLiteral("Transport is not open")));
    }
    return m_session->sendRequest(method, params, timeout);
}

void ProtocolPeer::notify(const QString &method, const QJsonObject &params)
{
    m_session->sendNotification(method, params);
}

void ProtocolPeer::bindNotification(
    const QString &method, std::function<void(const QJsonObject &)> handler)
{
    m_session->setNotificationHandler(method, std::move(handler));
}

void ProtocolPeer::warnOnUnknownVersion(
    const QString &negotiated, const QStringList &known, const QLoggingCategory &log)
{
    if (known.contains(negotiated))
        return;

    qCWarning(log).noquote()
        << QString("Unexpected protocol version from peer: %1 (known: %2)")
               .arg(negotiated, known.join(QLatin1String(", ")));
}

} // namespace LLMQore::Rpc
