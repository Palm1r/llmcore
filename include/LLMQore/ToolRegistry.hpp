// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

#include <LLMQore/LLMQore_global.h>

namespace LLMQore {

class BaseTool;

class LLMQORE_EXPORT ToolRegistry : public QObject
{
    Q_OBJECT
public:
    explicit ToolRegistry(QObject *parent = nullptr);

    void addTool(BaseTool *tool);
    void removeTool(const QString &name);
    void removeAllTools();
    void removeToolsIf(std::function<bool(const BaseTool *)> predicate);
    BaseTool *tool(const QString &name) const;
    QList<BaseTool *> registeredTools() const;

signals:
    void toolsChanged();

protected:
    // QMap for deterministic alphabetical iteration order — important for
    // reproducible test output and stable round-trips on the wire.
    QMap<QString, BaseTool *> m_tools;
};

} // namespace LLMQore
