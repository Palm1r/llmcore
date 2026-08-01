// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/McpProvisioning.hpp>

#include <LLMQore/Log.hpp>
#include <LLMQore/RpcStdioTransport.hpp>

namespace LLMQore::Mcp {

McpHttpSpec parseHttpSpec(const QString &spec)
{
    if (spec.isEmpty())
        return McpHttpSpec::Latest;
    if (spec == QLatin1String("2024-11-05"))
        return McpHttpSpec::V2024_11_05;
    if (spec == QLatin1String("2025-03-26") || spec == QLatin1String("2025-06-18")
        || spec == QLatin1String("2025-11-25") || spec == QLatin1String("latest"))
        return McpHttpSpec::V2025_03_26;

    qCWarning(llmMcpLog).noquote()
        << QString("Unknown httpSpec '%1' -- falling back to latest.").arg(spec);
    return McpHttpSpec::Latest;
}

Rpc::Transport *makeTransport(const ServerEndpoint &endpoint, QObject *parent)
{
    if (endpoint.url.isValid()) {
        HttpTransportConfig cfg;
        cfg.endpoint = endpoint.url;
        cfg.headers = endpoint.headers;
        cfg.spec = parseHttpSpec(endpoint.httpSpec);
        return new McpHttpTransport(cfg, nullptr, parent);
    }

    if (!endpoint.command.isEmpty()) {
        Rpc::StdioLaunchConfig cfg;
        cfg.program = endpoint.command;
        cfg.arguments = endpoint.arguments;
        cfg.environment = endpoint.env;
        cfg.workingDirectory = endpoint.workingDirectory;
        return new Rpc::StdioClientTransport(cfg, parent);
    }

    qCWarning(llmMcpLog).noquote()
        << QString("MCP server '%1': neither a url nor a command was given")
               .arg(endpoint.name);
    return nullptr;
}

} // namespace LLMQore::Mcp
