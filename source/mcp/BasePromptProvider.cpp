// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BasePromptProvider.hpp>

#include <QPromise>

namespace LLMQore::Mcp {

QFuture<CompletionResult> BasePromptProvider::completeArgument(
    const QString & /*promptName*/,
    const QString & /*argumentName*/,
    const QString & /*partialValue*/,
    const QJsonObject & /*contextArguments*/)
{
    auto promise = std::make_shared<QPromise<CompletionResult>>();
    promise->start();
    promise->addResult(CompletionResult{});
    promise->finish();
    return promise->future();
}

} // namespace LLMQore::Mcp
