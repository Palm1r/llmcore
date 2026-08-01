// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "Usage.hpp"

namespace LLMQore {

namespace {

std::optional<int> readField(const QJsonObject &container, const UsageField &field)
{
    if (field.name.isEmpty())
        return std::nullopt;

    QJsonObject scope = container;
    if (!field.object.isEmpty()) {
        if (!container.contains(field.object))
            return std::nullopt;
        scope = container.value(field.object).toObject();
    }

    if (!scope.contains(field.name))
        return std::nullopt;

    return scope.value(field.name).toInt();
}

} // namespace

UsageDelta parseUsage(const QJsonObject &root, const UsageSchema &schema)
{
    QJsonObject container = root;
    if (!schema.container.isEmpty()) {
        if (!root.contains(schema.container))
            return {};
        container = root.value(schema.container).toObject();
    }

    UsageDelta delta;
    delta.promptTokens = readField(container, schema.prompt);
    delta.completionTokens = readField(container, schema.completion);
    delta.cachedPromptTokens = readField(container, schema.cached);
    delta.reasoningTokens = readField(container, schema.reasoning);
    return delta;
}

TokenUsage applyTo(const UsageDelta &delta, const TokenUsage &base)
{
    TokenUsage merged = base;
    if (delta.promptTokens)
        merged.promptTokens = *delta.promptTokens;
    if (delta.completionTokens)
        merged.completionTokens = *delta.completionTokens;
    if (delta.cachedPromptTokens)
        merged.cachedPromptTokens = *delta.cachedPromptTokens;
    if (delta.reasoningTokens)
        merged.reasoningTokens = *delta.reasoningTokens;
    return merged;
}

} // namespace LLMQore
