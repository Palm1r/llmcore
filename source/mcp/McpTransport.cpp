// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/McpTransport.hpp>

namespace LLMQore::Mcp {

McpTransport::McpTransport(QObject *parent)
    : QObject(parent)
{}

void McpTransport::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

} // namespace LLMQore::Mcp
