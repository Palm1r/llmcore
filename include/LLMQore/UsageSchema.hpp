// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QLatin1String>

#include <LLMQore/LLMQore_global.h>

namespace LLMQore {

struct UsageField
{
    QLatin1String object;
    QLatin1String name;
};

struct UsageSchema
{
    QLatin1String container;
    UsageField prompt;
    UsageField completion;
    UsageField cached;
    UsageField reasoning;
};

inline constexpr UsageSchema kNoUsageSchema{};

} // namespace LLMQore
