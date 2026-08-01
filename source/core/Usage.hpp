// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

#include <QJsonObject>

#include <LLMQore/BaseClient.hpp>
#include <LLMQore/UsageSchema.hpp>

namespace LLMQore {

// What one response object actually said about token usage. A counter the
// response did not mention stays unset, so a later object cannot zero it.
struct UsageDelta
{
    std::optional<int> promptTokens;
    std::optional<int> completionTokens;
    std::optional<int> cachedPromptTokens;
    std::optional<int> reasoningTokens;

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return !promptTokens && !completionTokens && !cachedPromptTokens && !reasoningTokens;
    }
};

[[nodiscard]] UsageDelta parseUsage(const QJsonObject &root, const UsageSchema &schema);

[[nodiscard]] TokenUsage applyTo(const UsageDelta &delta, const TokenUsage &base);

} // namespace LLMQore
