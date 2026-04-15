// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseTool.hpp>

namespace LLMQore {

BaseTool::BaseTool(QObject *parent)
    : QObject(parent)
{}

bool BaseTool::isEnabled() const
{
    return m_enabled;
}

void BaseTool::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

} // namespace LLMQore
