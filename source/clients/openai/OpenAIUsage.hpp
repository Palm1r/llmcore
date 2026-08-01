// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonObject>

#include <LLMQore/BaseClient.hpp>

namespace LLMQore {

// Reads the OpenAI-compatible "usage" object shared by OpenAI, Mistral,
// DeepSeek and llama.cpp's OpenAI-mode endpoints.
[[nodiscard]] inline TokenUsage parseOpenAIUsage(const QJsonObject &usage)
{
    TokenUsage u;
    u.promptTokens = usage.value("prompt_tokens").toInt();
    u.completionTokens = usage.value("completion_tokens").toInt();
    u.cachedPromptTokens
        = usage.value("prompt_tokens_details").toObject().value("cached_tokens").toInt();
    u.reasoningTokens
        = usage.value("completion_tokens_details").toObject().value("reasoning_tokens").toInt();
    return u;
}

} // namespace LLMQore
