// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <LLMQore/LLMQore_global.h>
#include <LLMQore/McpTypes.hpp>

namespace LLMQore::Mcp {

class LLMQORE_EXPORT BasePromptProvider : public QObject
{
    Q_OBJECT
public:
    explicit BasePromptProvider(QObject *parent = nullptr)
        : QObject(parent)
    {}
    ~BasePromptProvider() override = default;

    virtual QFuture<QList<PromptInfo>> listPrompts() = 0;

    virtual QFuture<PromptGetResult> getPrompt(
        const QString &name, const QJsonObject &arguments)
        = 0;

    virtual QFuture<CompletionResult> completeArgument(
        const QString &promptName,
        const QString &argumentName,
        const QString &partialValue,
        const QJsonObject &contextArguments);

signals:
    void listChanged();
};

} // namespace LLMQore::Mcp
