// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>

#include <LLMQore/LLMQore_global.h>
#include <LLMQore/RpcLineFramer.hpp>

namespace LLMQore::Rpc {

class LLMQORE_EXPORT NdJsonCodec
{
public:
    static QByteArray encode(const QJsonObject &message);

    QList<QJsonObject> decode(const QByteArray &data);

    void clear();

    [[nodiscard]] bool hasIncompleteData() const;

    void setMaxBufferBytes(qsizetype bytes) noexcept { m_framer.setMaxBufferBytes(bytes); }
    [[nodiscard]] qsizetype maxBufferBytes() const noexcept { return m_framer.maxBufferBytes(); }

private:
    LineFramer m_framer;
};

} // namespace LLMQore::Rpc
