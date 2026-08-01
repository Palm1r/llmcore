// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QByteArrayList>

#include <LLMQore/LLMQore_global.h>

namespace LLMQore {

class LLMQORE_EXPORT LineBuffer
{
public:
    LineBuffer() = default;

    QByteArrayList processData(const QByteArray &data);

    void clear();

    [[nodiscard]] QByteArray currentBuffer() const;
    [[nodiscard]] bool hasIncompleteData() const;

private:
    QByteArray m_buffer;
};

} // namespace LLMQore
