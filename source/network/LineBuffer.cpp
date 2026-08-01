// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/LineBuffer.hpp>

namespace LLMQore {

QByteArrayList LineBuffer::processData(const QByteArray &data)
{
    m_buffer += data;

    QByteArrayList lines;
    int start = 0;
    for (int i = m_buffer.indexOf('\n'); i >= 0; i = m_buffer.indexOf('\n', start)) {
        lines.append(m_buffer.mid(start, i - start));
        start = i + 1;
    }
    m_buffer.remove(0, start);

    return lines;
}

void LineBuffer::clear()
{
    m_buffer.clear();
}

QByteArray LineBuffer::currentBuffer() const
{
    return m_buffer;
}

bool LineBuffer::hasIncompleteData() const
{
    return !m_buffer.isEmpty();
}

} // namespace LLMQore
