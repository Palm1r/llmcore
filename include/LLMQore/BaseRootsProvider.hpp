// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QList>
#include <QObject>

#include <LLMQore/LLMQore_global.h>
#include <LLMQore/McpTypes.hpp>

namespace LLMQore::Mcp {

class LLMQORE_EXPORT BaseRootsProvider : public QObject
{
    Q_OBJECT
public:
    explicit BaseRootsProvider(QObject *parent = nullptr)
        : QObject(parent)
    {}
    ~BaseRootsProvider() override = default;

    virtual QFuture<QList<Root>> listRoots() = 0;

signals:
    void listChanged();
};

} // namespace LLMQore::Mcp
