// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/LineBuffer.hpp>

#include <LLMQore/Log.hpp>

namespace LLMQore {

QByteArrayList LineBuffer::processData(const QByteArray &data)
{
    m_buffer += data;

    QByteArrayList lines;
    qsizetype start = 0;
    for (qsizetype i = m_buffer.indexOf('\n'); i >= 0; i = m_buffer.indexOf('\n', start)) {
        lines.append(m_buffer.mid(start, i - start));
        start = i + 1;
    }
    m_buffer.remove(0, start);

    if (m_maxBufferBytes > 0 && m_buffer.size() > m_maxBufferBytes) {
        qCWarning(llmNetworkLog).noquote()
            << QString("Line buffer exceeded %1 bytes with no line terminator; dropping")
                   .arg(m_maxBufferBytes);
        m_buffer.clear();
    }

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
