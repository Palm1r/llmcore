// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPair>
#include <QString>

#include <LLMQore/LLMQore_global.h>

#include <LLMQore/BaseTool.hpp>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore {

class LLMQORE_EXPORT ToolHandler : public QObject
{
    Q_OBJECT

public:
    explicit ToolHandler(QObject *parent = nullptr);

    QFuture<ToolResult> executeToolAsync(
        const QString &requestId, const QString &toolId, BaseTool *tool, const QJsonObject &input);

    void cleanupRequest(const QString &requestId);

signals:
    void toolCompleted(
        const QString &requestId, const QString &toolId, const LLMQore::ToolResult &result);
    void toolFailed(const QString &requestId, const QString &toolId, const QString &error);

private:
    struct ToolExecution
    {
        QString requestId;
        QString toolId;
        QString toolName;
        QFutureWatcher<ToolResult> *watcher;
    };

    // Keyed by request as well as tool: two concurrent requests routinely carry
    // the same provider-minted id (`toolu_1`), and a bare toolId collides.
    using ExecutionKey = QPair<QString, QString>;

    QHash<ExecutionKey, ToolExecution *> m_activeExecutions;

    void onToolExecutionFinished(const ExecutionKey &key);
};

} // namespace LLMQore
