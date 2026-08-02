// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

#include <LLMQore/LLMQore_global.h>

namespace LLMQore {

class BaseTool;

struct LLMQORE_EXPORT ToolSnapshot
{
    QString id;
    QString displayName;
    QString description;
};

class LLMQORE_EXPORT ToolRegistry : public QObject
{
    Q_OBJECT
public:
    explicit ToolRegistry(QObject *parent = nullptr);

    void addTool(BaseTool *tool);
    void removeTool(const QString &name);
    void removeAllTools();
    BaseTool *tool(const QString &name) const;

    QList<BaseTool *> registeredTools() const;

    QList<ToolSnapshot> toolsSnapshot() const;

signals:
    void toolsChanged();

protected:
    QMap<QString, BaseTool *> m_tools;
};

} // namespace LLMQore
