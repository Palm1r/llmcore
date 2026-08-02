// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <memory>

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
#include <LLMQore/McpTypes.hpp>
#include <LLMQore/ToolRegistry.hpp>
#include <LLMQore/ToolResult.hpp>
#include <LLMQore/ToolDialect.hpp>

namespace LLMQore::Mcp {
class McpClient;
class McpToolBinder;
}

namespace LLMQore {

class ToolHandler;
class ToolsManager;

struct PendingTool
{
    QString id;
    QString name;
    QJsonObject input;
    ToolResult result;
    QString resultText;
    bool complete = false;
};

class LLMQORE_EXPORT ToolRound
{
public:
    struct Call
    {
        QString id;
        QString name;
        QJsonObject input;
    };

    ToolRound() = default;
    ToolRound(ToolsManager *manager, QString requestId);

    void addCalls(const QList<Call> &calls);
    void advance();
    bool settle(const QString &toolId, const ToolResult &result, bool success);
    void abandon();

    bool isAdvancing() const { return m_advancing; }
    bool isClosed() const { return m_closed; }
    bool isSettled() const;
    void close() { m_closed = true; }

    const PendingTool *find(const QString &toolId) const;
    QHash<QString, ToolResult> results() const;

private:
    bool dispatch(int index);

    ToolsManager *m_manager = nullptr;
    QString m_requestId;
    QList<PendingTool> m_pending;
    QHash<QString, int> m_indexById;
    int m_next = 0;
    bool m_advancing = false;
    bool m_closed = false;
};

class LLMQORE_EXPORT ToolsManager : public ToolRegistry
{
    Q_OBJECT

public:
    explicit ToolsManager(const ToolDialect &dialect, QObject *parent = nullptr);

    void setMcpClientInfo(Mcp::Implementation info);
    bool addMcpServer(const Mcp::ServerEndpoint &endpoint);
    int loadMcpServers(const QJsonObject &config);
    void addMcpClient(
        Mcp::McpClient *client, const QString &serverName = {}, bool autoReconnect = false);
    void removeMcpClient(Mcp::McpClient *client);
    void shutdownMcp();

    QJsonArray getToolsDefinitions() const;
    QString displayName(const QString &toolName) const;

    void beginRound(const QString &requestId, const QList<ToolRound::Call> &calls);
    void executeToolCall(
        const QString &requestId,
        const QString &toolId,
        const QString &toolName,
        const QJsonObject &input);
    void cleanupRequest(const QString &requestId);

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

    void mcpServerInitialized(const QString &name, const LLMQore::Mcp::InitializeResult &result);
    void mcpServerInitFailed(const QString &name, const QString &error);
    void mcpToolsSynced(const QString &name, int toolCount);
    void mcpServerDisconnected(const QString &name);

private slots:
    void onToolCompleted(
        const QString &requestId, const QString &toolId, const ToolResult &result);
    void onToolErrored(
        const QString &requestId, const QString &toolId, const QString &errorText);

private:
    friend class ToolRound;

    void initConnections();
    void driveRound(const QString &requestId);
    void startExecution(
        const QString &requestId,
        const QString &toolId,
        const QString &toolName,
        const QJsonObject &input);
    void finalizePendingTool(
        const QString &requestId, const QString &toolId, const ToolResult &rich, bool success);
    QJsonArray buildToolsDefinitions() const;

    ToolHandler *m_toolHandler;
    const ToolDialect &m_dialect;
    QHash<QString, std::shared_ptr<ToolRound>> m_toolRounds;
    ExecutionGate m_executionGate;

    Mcp::McpToolBinder *m_binder = nullptr;
};

} // namespace LLMQore
