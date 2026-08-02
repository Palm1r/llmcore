// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/RpcNdJsonCodec.hpp>

#include <QJsonDocument>

#include <LLMQore/Log.hpp>

namespace LLMQore::Rpc {

QByteArray NdJsonCodec::encode(const QJsonObject &message)
{
    return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
}

QList<QJsonObject> NdJsonCodec::decode(const QByteArray &data)
{
    QList<QJsonObject> messages;

    const QByteArrayList lines = m_framer.append(data);
    for (const QByteArray &line : lines) {
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(llmRpcLog).noquote()
                << QString("Dropping invalid JSON line: %1").arg(QString::fromUtf8(line));
            continue;
        }
        messages.append(doc.object());
    }

    return messages;
}

void NdJsonCodec::clear()
{
    m_framer.clear();
}

bool NdJsonCodec::hasIncompleteData() const
{
    return m_framer.hasIncompleteData();
}

} // namespace LLMQore::Rpc
