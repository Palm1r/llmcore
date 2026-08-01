// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/McpRemoteTool.hpp>

#include <LLMQore/McpClient.hpp>
#include <LLMQore/RpcExceptions.hpp>

#include <LLMQore/FutureUtils.hpp>
#include <QPromise>

namespace LLMQore::Mcp {

McpRemoteTool::McpRemoteTool(
    McpClient *client, const QString &serverName, ToolInfo info, QObject *parent)
    : LLMQore::BaseTool(parent)
    , m_client(client)
    , m_serverName(serverName)
    , m_info(std::move(info))
{}

QString McpRemoteTool::composeId(const QString &serverName, const QString &toolName)
{
    return serverName.isEmpty()
               ? toolName
               : QStringLiteral("%1_%2").arg(serverName, toolName);
}

QString McpRemoteTool::id() const
{
    return composeId(m_serverName, m_info.name);
}

QString McpRemoteTool::displayName() const
{
    return m_info.title.isEmpty() ? m_info.name : m_info.title;
}

QString McpRemoteTool::description() const
{
    return m_info.description;
}

QJsonObject McpRemoteTool::parametersSchema() const
{
    return m_info.inputSchema;
}

QFuture<LLMQore::ToolResult> McpRemoteTool::executeAsync(const QJsonObject &input)
{
    if (!m_client) {
        return LLMQore::readyFuture(LLMQore::ToolResult::error(
            QStringLiteral("MCP client is not available")));
    }

    return LLMQore::compat(m_client->callTool(m_info.name, input))
        .then(this, [](const LLMQore::ToolResult &result) { return result; })
        .onFailed(this, [](const auto &e) {
            if constexpr (std::is_same_v<std::decay_t<decltype(e)>, Rpc::JsonRpcException>)
                return LLMQore::ToolResult::error(e.message());
            return LLMQore::ToolResult::error(QString::fromUtf8(e.what()));
        });
}

} // namespace LLMQore::Mcp
