// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "ToolHandler.hpp"
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/McpClient.hpp>
#include <LLMQore/McpToolBinder.hpp>
#include <LLMQore/ToolsManager.hpp>

namespace LLMQore {

ToolsManager::ToolsManager(const ToolDialect &dialect, QObject *parent)
    : ToolRegistry(parent)
    , m_toolHandler(new ToolHandler(this))
    , m_dialect(dialect)
    , m_binder(new Mcp::McpToolBinder(this, this))
{
    initConnections();
}

void ToolsManager::initConnections()
{
    connect(m_toolHandler, &ToolHandler::toolCompleted, this, &ToolsManager::onToolCompleted);
    connect(m_toolHandler, &ToolHandler::toolFailed, this, &ToolsManager::onToolErrored);
}

void ToolsManager::addMcpServer(const Mcp::ServerEndpoint &endpoint)
{
    m_binder->addServer(endpoint);
}

void ToolsManager::loadMcpServers(const QJsonObject &config)
{
    m_binder->loadServers(config);
}

void ToolsManager::addMcpClient(Mcp::McpClient *client)
{
    m_binder->addClient(client);
}

void ToolsManager::removeMcpClient(Mcp::McpClient *client)
{
    m_binder->removeClient(client);
}

QString ToolsManager::displayName(const QString &toolName) const
{
    if (auto *t = m_tools.value(toolName)) {
        return t->displayName();
    }
    return QStringLiteral("Unknown tool");
}

void ToolsManager::executeToolCall(
    const QString &requestId,
    const QString &toolId,
    const QString &toolName,
    const QJsonObject &input)
{
    qCDebug(llmToolsLog).noquote()
        << QString("Queueing tool %1 (ID: %2) for request %3").arg(toolName, toolId, requestId);

    if (!m_toolRounds.contains(requestId)) {
        m_toolRounds[requestId] = ToolRound();
    }

    auto &queue = m_toolRounds[requestId];

    for (const auto &tool : queue.queue) {
        if (tool.id == toolId) {
            qCDebug(llmToolsLog).noquote()
                << QString("Tool %1 already in queue for request %2").arg(toolId, requestId);
            return;
        }
    }

    if (queue.completed.contains(toolId)) {
        qCDebug(llmToolsLog).noquote()
            << QString("Tool %1 already completed for request %2").arg(toolId, requestId);
        return;
    }

    PendingTool pendingTool;
    pendingTool.id = toolId;
    pendingTool.name = toolName;
    pendingTool.input = input;
    queue.queue.append(pendingTool);

    qCDebug(llmToolsLog).noquote()
        << QString("Tool %1 added to queue (position %2)").arg(toolName).arg(queue.queue.size());

    if (!queue.isExecuting) {
        executeNextTool(requestId);
    }
}

void ToolsManager::setExecutionGate(ExecutionGate gate)
{
    m_executionGate = std::move(gate);
}

void ToolsManager::executeNextTool(const QString &requestId)
{
    if (!m_toolRounds.contains(requestId)) {
        return;
    }

    auto &queue = m_toolRounds[requestId];

    while (!queue.queue.isEmpty()) {
        PendingTool pendingTool = queue.queue.takeFirst();
        queue.isExecuting = true;

        BaseTool *toolInstance = m_tools.value(pendingTool.name);
        if (!toolInstance) {
            qCWarning(llmToolsLog).noquote()
                << QString("Tool not found: %1").arg(pendingTool.name);
            const QString errText
                = QString("Error: Tool not found: %1").arg(pendingTool.name);
            pendingTool.result = ToolResult::error(errText);
            pendingTool.resultText = errText;
            pendingTool.complete = true;
            queue.completed[pendingTool.id] = pendingTool;
            continue;
        }

        pendingTool.complete = false;
        queue.completed[pendingTool.id] = pendingTool;

        qCDebug(llmToolsLog).noquote()
            << QString("Executing tool %1 (ID: %2) for request %3 (%4 remaining)")
                   .arg(pendingTool.name, pendingTool.id, requestId)
                   .arg(queue.queue.size());

        if (m_executionGate && toolInstance->safety() == ToolSafety::Mutating) {
            const QString gatedId = pendingTool.id;
            const QString gatedName = pendingTool.name;
            const QJsonObject gatedInput = pendingTool.input;

            LLMQore::futureThen(
                this,
                m_executionGate(requestId, gatedId, gatedName, gatedInput),
                [this, requestId, gatedId, gatedName, gatedInput](bool allowed) {
                    if (!allowed) {
                        qCDebug(llmToolsLog).noquote()
                            << QString("Tool %1 was declined before execution").arg(gatedName);
                        finalizePendingTool(
                            requestId,
                            gatedId,
                            ToolResult::error(
                                QStringLiteral("The user declined to run %1").arg(gatedName)),
                            /*success*/ false);
                        return;
                    }

                    BaseTool *gatedInstance = m_tools.value(gatedName);
                    if (!gatedInstance) {
                        finalizePendingTool(
                            requestId,
                            gatedId,
                            ToolResult::error(
                                QStringLiteral("Tool not found: %1").arg(gatedName)),
                            /*success*/ false);
                        return;
                    }

                    emit toolExecutionStarted(requestId, gatedId, gatedName, gatedInput);
                    m_toolHandler->executeToolAsync(requestId, gatedId, gatedInstance, gatedInput);
                });
            return;
        }

        emit toolExecutionStarted(
            requestId, pendingTool.id, pendingTool.name, pendingTool.input);

        m_toolHandler->executeToolAsync(requestId, pendingTool.id, toolInstance, pendingTool.input);
        qCDebug(llmToolsLog).noquote()
            << QString("Started async execution of %1").arg(pendingTool.name);
        return;
    }

    qCDebug(llmToolsLog).noquote()
        << QString("All tools complete for request %1, emitting results").arg(requestId);

    const QHash<QString, ToolResult> results = getToolResults(requestId);

    queue.beginNextRound();
    queue.isExecuting = false;

    emit toolExecutionComplete(requestId, results);
}

QJsonArray ToolsManager::getToolsDefinitions() const
{
    return buildToolsDefinitions();
}

QJsonArray ToolsManager::buildToolsDefinitions() const
{
    QJsonArray toolsArray;

    for (auto it = m_tools.constBegin(); it != m_tools.constEnd(); ++it) {
        BaseTool *t = it.value();
        if (!t || !t->isEnabled()) {
            continue;
        }

        toolsArray.append(m_dialect.wrapDefinition(*t));
    }

    return m_dialect.finalizeDefinitions(std::move(toolsArray));
}

void ToolsManager::cleanupRequest(const QString &requestId)
{
    if (m_toolRounds.contains(requestId)) {
        m_toolHandler->cleanupRequest(requestId);
        m_toolRounds.remove(requestId);
    }
}

void ToolsManager::onToolCompleted(
    const QString &requestId, const QString &toolId, const ToolResult &result)
{
    finalizePendingTool(requestId, toolId, result, /*success*/ true);
}

void ToolsManager::onToolErrored(
    const QString &requestId, const QString &toolId, const QString &errorText)
{
    finalizePendingTool(
        requestId, toolId, ToolResult::error(errorText), /*success*/ false);
}

void ToolsManager::finalizePendingTool(
    const QString &requestId, const QString &toolId, const ToolResult &rich, bool success)
{
    if (!m_toolRounds.contains(requestId))
        return;

    auto &queue = m_toolRounds[requestId];
    if (!queue.completed.contains(toolId))
        return;

    PendingTool &pendingTool = queue.completed[toolId];
    pendingTool.result = rich;
    pendingTool.resultText
        = success ? rich.asText() : QString("Error: %1").arg(rich.asText());
    pendingTool.complete = true;

    qCDebug(llmToolsLog).noquote() << QString("Tool %1 %2 for request %3")
                                          .arg(toolId)
                                          .arg(success ? QString("completed") : QString("failed"))
                                          .arg(requestId);

    emit toolExecutionResult(requestId, toolId, pendingTool.name, pendingTool.resultText);

    executeNextTool(requestId);
}

QHash<QString, ToolResult> ToolsManager::getToolResults(const QString &requestId) const
{
    QHash<QString, ToolResult> results;

    if (m_toolRounds.contains(requestId)) {
        const auto &queue = m_toolRounds[requestId];
        for (auto it = queue.completed.begin(); it != queue.completed.end(); ++it) {
            if (it.value().complete)
                results[it.key()] = it.value().result;
        }
    }

    return results;
}

} // namespace LLMQore
