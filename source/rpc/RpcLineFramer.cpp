// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/RpcLineFramer.hpp>

#include <LLMQore/Log.hpp>

namespace LLMQore::Rpc {

QByteArrayList LineFramer::append(const QByteArray &data)
{
    QByteArrayList lines;
    m_buffer.append(data);

    qsizetype start = 0;
    while (true) {
        const qsizetype nl = m_buffer.indexOf('\n', start);
        if (nl < 0)
            break;

        qsizetype end = nl;
        if (end > start && m_buffer.at(end - 1) == '\r')
            --end;

        QByteArray line = m_buffer.mid(start, end - start);
        if (!line.isEmpty())
            lines.append(line);

        start = nl + 1;
    }

    if (start > 0)
        m_buffer.remove(0, start);

    if (m_maxBufferBytes > 0 && m_buffer.size() > m_maxBufferBytes) {
        qCWarning(llmNetworkLog).noquote()
            << QString("Line framer exceeded %1 bytes with no line terminator; dropping")
                   .arg(m_maxBufferBytes);
        m_buffer.clear();
    }

    return lines;
}

void LineFramer::clear()
{
    m_buffer.clear();
}

QByteArray LineFramer::currentBuffer() const
{
    return m_buffer;
}

bool LineFramer::hasIncompleteData() const
{
    return !m_buffer.isEmpty();
}

} // namespace LLMQore::Rpc
