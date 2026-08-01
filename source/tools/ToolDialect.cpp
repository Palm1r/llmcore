// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/ToolDialect.hpp>

namespace LLMQore {

ToolDialect::~ToolDialect() = default;

QJsonArray ToolDialect::finalizeDefinitions(QJsonArray definitions) const
{
    return definitions;
}

} // namespace LLMQore
