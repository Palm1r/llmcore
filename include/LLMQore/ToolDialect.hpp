// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonArray>
#include <QJsonObject>

#include <LLMQore/LLMQore_global.h>

namespace LLMQore {

class BaseTool;

class LLMQORE_EXPORT ToolDialect
{
public:
    virtual ~ToolDialect();

    [[nodiscard]] virtual QJsonObject wrapDefinition(const BaseTool &tool) const = 0;

    [[nodiscard]] virtual QJsonArray finalizeDefinitions(QJsonArray definitions) const;
};

} // namespace LLMQore
