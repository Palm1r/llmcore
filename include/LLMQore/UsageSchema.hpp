// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QLatin1String>

#include <LLMQore/LLMQore_global.h>

namespace LLMQore {

// Where one token counter lives inside the usage container. An empty `object`
// means the counter sits directly in the container; an empty `name` means the
// provider does not report that counter at all.
struct UsageField
{
    QLatin1String object;
    QLatin1String name;
};

// How one provider spells token usage on the wire. An empty `container` means
// the counters are at the root of the response object.
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
