// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QByteArrayList>

#include <LLMQore/LLMQore_global.h>

namespace LLMQore::Rpc {

class LLMQORE_EXPORT LineFramer
{
public:
    static constexpr qsizetype kDefaultMaxBufferBytes = 16 * 1024 * 1024;

    LineFramer() = default;

    QByteArrayList append(const QByteArray &data);

    void clear();

    [[nodiscard]] QByteArray currentBuffer() const;
    [[nodiscard]] bool hasIncompleteData() const;

    void setMaxBufferBytes(qsizetype bytes) noexcept { m_maxBufferBytes = bytes; }
    [[nodiscard]] qsizetype maxBufferBytes() const noexcept { return m_maxBufferBytes; }

private:
    QByteArray m_buffer;
    qsizetype m_maxBufferBytes = kDefaultMaxBufferBytes;
};

} // namespace LLMQore::Rpc
