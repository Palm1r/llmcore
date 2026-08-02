// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BasePromptProvider.hpp>

#include <LLMQore/FutureUtils.hpp>

namespace LLMQore::Mcp {

QFuture<CompletionResult> BasePromptProvider::completeArgument(
    const QString & /*promptName*/,
    const QString & /*argumentName*/,
    const QString & /*partialValue*/,
    const QJsonObject & /*contextArguments*/)
{
    return LLMQore::readyFuture(CompletionResult{});
}

} // namespace LLMQore::Mcp
