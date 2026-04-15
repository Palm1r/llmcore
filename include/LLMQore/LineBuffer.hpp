// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QStringList>

#include <LLMQore/LLMQore_global.h>

namespace LLMQore {

class LLMQORE_EXPORT LineBuffer
{
public:
    LineBuffer() = default;

    QStringList processData(const QByteArray &data);

    void clear();

    [[nodiscard]] QString currentBuffer() const;
    [[nodiscard]] bool hasIncompleteData() const;

private:
    QString m_buffer;
};

} // namespace LLMQore
