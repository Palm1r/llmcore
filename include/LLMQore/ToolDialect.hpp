// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonArray>
#include <QJsonObject>

#include <LLMQore/LLMQore_global.h>

namespace LLMQore {

class BaseTool;

// How one provider spells tool definitions on the wire. Lives next to that
// provider's message translator, so both directions of the format -- schema
// out, results back -- are readable in one file.
class LLMQORE_EXPORT ToolDialect
{
public:
    virtual ~ToolDialect();

    [[nodiscard]] virtual QJsonObject wrapDefinition(const BaseTool &tool) const = 0;

    // Envelope around the whole array, for providers that need one.
    [[nodiscard]] virtual QJsonArray finalizeDefinitions(QJsonArray definitions) const;
};

} // namespace LLMQore
