// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "ToolHandler.hpp"
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/McpClient.hpp>
#include <LLMQore/McpToolBinder.hpp>
#include <LLMQore/ToolsManager.hpp>

namespace LLMQore {

ToolRound::ToolRound(ToolsManager *manager, QString requestId)
    : m_manager(manager)
    , m_requestId(std::move(requestId))
{}

void ToolRound::addCalls(const QList<ToolCall> &calls)
{
    for (const ToolCall &call : calls) {
        if (m_indexById.contains(call.id)) {
            qCDebug(llmToolsLog).noquote()
                << QString("Tool %1 already in round for request %2").arg(call.id, m_requestId);
            continue;
        }

        PendingTool pending;
        pending.id = call.id;
        pending.name = call.name;
        pending.input = call.input;

        m_indexById.insert(call.id, m_pending.size());
        m_pending.append(pending);

        qCDebug(llmToolsLog).noquote()
            << QString("Queueing tool %1 (ID: %2) for request %3")
                   .arg(call.name, call.id, m_requestId);
    }
}

void ToolRound::advance()
{
    m_advancing = true;
    while (m_next < m_pending.size()) {
        if (dispatch(m_next++))
            break;
    }
    m_advancing = false;
}

bool ToolRound::dispatch(int index)
{
    const QString toolId = m_pending[index].id;
    const QString toolName = m_pending[index].name;
    const QJsonObject input = m_pending[index].input;

    BaseTool *instance = m_manager->m_tools.value(toolName);
    if (!instance) {
        qCWarning(llmToolsLog).noquote() << QString("Tool not found: %1").arg(toolName);
        m_manager->finalizePendingTool(
            m_requestId,
            toolId,
            ToolResult::error(QStringLiteral("Error: Tool not found: %1").arg(toolName)),
            /*success*/ false);
        return false;
    }

    qCDebug(llmToolsLog).noquote()
        << QString("Executing tool %1 (ID: %2) for request %3 (%4 remaining)")
               .arg(toolName, toolId, m_requestId)
               .arg(m_pending.size() - index - 1);

    if (m_manager->m_executionGate && instance->safety() == ToolSafety::Mutating) {
        ToolsManager *manager = m_manager;
        const QString requestId = m_requestId;

        LLMQore::futureThen(
            manager,
            manager->m_executionGate(requestId, toolId, toolName, input),
            [manager, requestId, toolId, toolName, input](bool allowed) {
                if (!allowed) {
                    qCDebug(llmToolsLog).noquote()
                        << QString("Tool %1 was declined before execution").arg(toolName);
                    manager->finalizePendingTool(
                        requestId,
                        toolId,
                        ToolResult::error(
                            QStringLiteral("The user declined to run %1").arg(toolName)),
                        /*success*/ false);
                    return;
                }

                manager->startExecution(requestId, toolId, toolName, input);
            });
        return true;
    }

    m_manager->startExecution(m_requestId, toolId, toolName, input);
    return true;
}

bool ToolRound::settle(const QString &toolId, const ToolResult &result, bool success)
{
    const int index = m_indexById.value(toolId, -1);
    if (index < 0 || index >= m_pending.size() || m_pending[index].complete)
        return false;

    const QString text = result.asText();

    PendingTool &pending = m_pending[index];
    pending.result = result;
    pending.resultText = success || text.startsWith(QStringLiteral("Error:"))
        ? text
        : QString("Error: %1").arg(text);
    pending.complete = true;
    return true;
}

void ToolRound::abandon()
{
    m_closed = true;
    m_pending.clear();
    m_indexById.clear();
    m_next = 0;

    if (m_manager)
        m_manager->m_toolHandler->cleanupRequest(m_requestId);
}

bool ToolRound::isSettled() const
{
    if (m_pending.isEmpty())
        return false;

    for (const PendingTool &pending : m_pending) {
        if (!pending.complete)
            return false;
    }
    return true;
}

const PendingTool *ToolRound::find(const QString &toolId) const
{
    const int index = m_indexById.value(toolId, -1);
    return index < 0 || index >= m_pending.size() ? nullptr : &m_pending[index];
}

QHash<QString, ToolResult> ToolRound::results() const
{
    QHash<QString, ToolResult> results;
    for (const PendingTool &pending : m_pending) {
        if (pending.complete)
            results.insert(pending.id, pending.result);
    }
    return results;
}

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

void ToolsManager::beginRound(const QString &requestId, const QList<ToolCall> &calls)
{
    if (calls.isEmpty())
        return;

    std::shared_ptr<ToolRound> round = m_toolRounds.value(requestId);
    if (!round || round->isClosed()) {
        round = std::make_shared<ToolRound>(this, requestId);
        m_toolRounds.insert(requestId, round);
    }

    round->addCalls(calls);
    driveRound(requestId);
}

void ToolsManager::executeToolCall(
    const QString &requestId,
    const QString &toolId,
    const QString &toolName,
    const QJsonObject &input)
{
    beginRound(requestId, {ToolCall{toolId, toolName, input}});
}

void ToolsManager::setExecutionGate(ExecutionGate gate)
{
    m_executionGate = std::move(gate);
}

void ToolsManager::driveRound(const QString &requestId)
{
    const std::shared_ptr<ToolRound> round = m_toolRounds.value(requestId);
    if (!round || round->isClosed() || round->isAdvancing())
        return;

    round->advance();

    if (round->isClosed() || !round->isSettled())
        return;

    qCDebug(llmToolsLog).noquote()
        << QString("All tools complete for request %1, emitting results").arg(requestId);

    const QHash<QString, ToolResult> results = round->results();
    round->close();

    emit toolExecutionComplete(requestId, results);
}

void ToolsManager::startExecution(
    const QString &requestId,
    const QString &toolId,
    const QString &toolName,
    const QJsonObject &input)
{
    BaseTool *instance = m_tools.value(toolName);
    if (!instance) {
        finalizePendingTool(
            requestId,
            toolId,
            ToolResult::error(QStringLiteral("Tool not found: %1").arg(toolName)),
            /*success*/ false);
        return;
    }

    emit toolExecutionStarted(requestId, toolId, toolName, input);

    m_toolHandler->executeToolAsync(requestId, toolId, instance, input);
    qCDebug(llmToolsLog).noquote()
        << QString("Started async execution of %1").arg(toolName);
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
    const std::shared_ptr<ToolRound> round = m_toolRounds.take(requestId);
    if (round)
        round->abandon();
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
    const std::shared_ptr<ToolRound> round = m_toolRounds.value(requestId);
    if (!round || !round->settle(toolId, rich, success))
        return;

    const PendingTool *pending = round->find(toolId);
    if (!pending)
        return;

    const QString toolName = pending->name;
    const QString resultText = pending->resultText;

    qCDebug(llmToolsLog).noquote() << QString("Tool %1 %2 for request %3")
                                          .arg(toolId)
                                          .arg(success ? QString("completed") : QString("failed"))
                                          .arg(requestId);

    emit toolExecutionResult(requestId, toolId, toolName, resultText);

    driveRound(requestId);
}

} // namespace LLMQore
