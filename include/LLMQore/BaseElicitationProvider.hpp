// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QObject>

#include <LLMQore/LLMQore_global.h>
#include <LLMQore/McpTypes.hpp>

namespace LLMQore::Mcp {

class LLMQORE_EXPORT BaseElicitationProvider : public QObject
{
    Q_OBJECT
public:
    explicit BaseElicitationProvider(QObject *parent = nullptr)
        : QObject(parent)
    {}
    ~BaseElicitationProvider() override = default;

    virtual QFuture<ElicitResult> elicit(const ElicitRequestParams &params) = 0;
};

} // namespace LLMQore::Mcp
