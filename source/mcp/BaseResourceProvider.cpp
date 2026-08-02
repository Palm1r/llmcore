// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseResourceProvider.hpp>

#include <LLMQore/FutureUtils.hpp>

namespace LLMQore::Mcp {

BaseResourceProvider::BaseResourceProvider(QObject *parent)
    : QObject(parent)
{}

QFuture<QList<ResourceTemplate>> BaseResourceProvider::listResourceTemplates()
{
    return LLMQore::readyFuture(QList<ResourceTemplate>{});
}

QFuture<CompletionResult> BaseResourceProvider::completeArgument(
    const QString & /*templateUri*/,
    const QString & /*placeholderName*/,
    const QString & /*partialValue*/,
    const QJsonObject & /*contextArguments*/)
{
    return LLMQore::readyFuture(CompletionResult{});
}

} // namespace LLMQore::Mcp
