// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

#include <QFuture>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <LLMQore/LLMQore_global.h>
#include <LLMQore/McpProvisioning.hpp>
#include <LLMQore/ToolRegistry.hpp>
#include <LLMQore/ToolResult.hpp>
#include <LLMQore/ToolDialect.hpp>

namespace LLMQore::Mcp {
class McpClient;
class McpToolBinder;
struct ToolInfo;
}

namespace LLMQore {

class ToolHandler;

struct PendingTool
{
    QString id;
    QString name;
    QJsonObject input;
    ToolResult result;
    QString resultText;
    bool complete = false;
};

// One turn of the agent loop. `completed` is the round's ledger, not the
// request's history: it is cleared at every round boundary, so a model that
// reuses a tool-call id in the next round is executed again instead of being
// swallowed by the dedup.
struct ToolRound
{
    QList<PendingTool> queue;
    QHash<QString, PendingTool> completed;
    bool isExecuting = false;

    void beginNextRound()
    {
        queue.clear();
        completed.clear();
    }
};

class LLMQORE_EXPORT ToolsManager : public ToolRegistry
{
    Q_OBJECT

public:
    explicit ToolsManager(const ToolDialect &dialect, QObject *parent = nullptr);

    void addMcpServer(const Mcp::ServerEndpoint &endpoint);
    void loadMcpServers(const QJsonObject &config);
    void addMcpClient(Mcp::McpClient *client);
    void removeMcpClient(Mcp::McpClient *client);

    QJsonArray getToolsDefinitions() const;
    QString displayName(const QString &toolName) const;

    void executeToolCall(
        const QString &requestId,
        const QString &toolId,
        const QString &toolName,
        const QJsonObject &input);
    void cleanupRequest(const QString &requestId);

    // Asked before a tool runs. Only consulted for tools that declare
    // ToolSafety::Mutating -- a read-only tool has nothing to approve.
    using ExecutionGate = std::function<QFuture<bool>(
        const QString &requestId,
        const QString &toolId,
        const QString &toolName,
        const QJsonObject &input)>;

    void setExecutionGate(ExecutionGate gate);

signals:
    void toolExecutionStarted(
        const QString &requestId,
        const QString &toolId,
        const QString &toolName,
        const QJsonObject &arguments);
    void toolExecutionResult(
        const QString &requestId,
        const QString &toolId,
        const QString &toolName,
        const QString &result);
    void toolExecutionComplete(
        const QString &requestId, const QHash<QString, ToolResult> &toolResults);

private slots:
    void onToolCompleted(
        const QString &requestId, const QString &toolId, const ToolResult &result);
    void onToolErrored(
        const QString &requestId, const QString &toolId, const QString &errorText);

private:
    void initConnections();
    void executeNextTool(const QString &requestId);
    void finalizePendingTool(
        const QString &requestId, const QString &toolId, const ToolResult &rich, bool success);
    // Results of the round now closing -- not of every round so far.
    QHash<QString, ToolResult> getToolResults(const QString &requestId) const;
    QJsonArray buildToolsDefinitions() const;

    ToolHandler *m_toolHandler;
    const ToolDialect &m_dialect;
    QHash<QString, ToolRound> m_toolRounds;
    ExecutionGate m_executionGate;

    Mcp::McpToolBinder *m_binder = nullptr;
};

} // namespace LLMQore
