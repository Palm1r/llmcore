// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/McpToolBinder.hpp>

#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/McpClient.hpp>
#include <LLMQore/McpRemoteTool.hpp>
#include <LLMQore/RpcTransport.hpp>
#include <LLMQore/ToolRegistry.hpp>

#include <QSet>
#include <QTimer>

namespace LLMQore::Mcp {

namespace {

constexpr int kInitialBackoffMs = 1000;
constexpr int kMaxBackoffMs = 30000;

QString prefixedToolId(const QString &serverName, const QString &toolName)
{
    return serverName.isEmpty()
               ? toolName
               : QStringLiteral("%1_%2").arg(serverName, toolName);
}

} // namespace

McpToolBinder::McpToolBinder(LLMQore::ToolRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
{
    Q_ASSERT(registry);
}

McpToolBinder::~McpToolBinder()
{
    m_stopping = true;
    for (auto it = m_bindings.begin(); it != m_bindings.end(); ++it) {
        if (it.key())
            disconnect(it.key(), nullptr, this, nullptr);
    }
}

void McpToolBinder::setClientInfo(Implementation info)
{
    m_clientInfo = std::move(info);
}

bool McpToolBinder::addServer(const ServerEndpoint &endpoint)
{
    Rpc::Transport *transport = makeTransport(endpoint, this);
    if (!transport)
        return false;

    auto *client = new McpClient(transport, m_clientInfo, this);
    attach(client, endpoint.name, /*owned*/ true, /*autoReconnect*/ true);
    initializeClient(client, /*isReconnect*/ false);
    return true;
}

int McpToolBinder::loadServers(const QJsonObject &config)
{
    int added = 0;
    const QList<ServerEndpoint> endpoints = parseServerMap(config);
    for (const ServerEndpoint &endpoint : endpoints) {
        if (addServer(endpoint))
            ++added;
    }
    return added;
}

void McpToolBinder::addClient(McpClient *client, const QString &serverName, bool autoReconnect)
{
    if (!client) {
        qCWarning(llmMcpLog).noquote() << "Attempted to bind null McpClient";
        return;
    }
    if (m_bindings.contains(client))
        return;

    attach(client, serverName, /*owned*/ false, autoReconnect);
    resyncClient(client);
}

void McpToolBinder::removeClient(McpClient *client)
{
    auto it = m_bindings.find(client);
    if (it == m_bindings.end())
        return;
    clearTools(it.value());
    if (client)
        disconnect(client, nullptr, this, nullptr);
    m_bindings.erase(it);
}

void McpToolBinder::shutdown()
{
    m_stopping = true;
    for (auto it = m_bindings.begin(); it != m_bindings.end(); ++it) {
        if (it->owned && it.key())
            it.key()->shutdown();
    }
}

void McpToolBinder::attach(McpClient *client, const QString &name, bool owned, bool autoReconnect)
{
    Binding binding;
    binding.name = name;
    binding.owned = owned;
    binding.autoReconnect = autoReconnect;
    binding.backoffMs = kInitialBackoffMs;
    m_bindings.insert(client, binding);

    connect(client, &McpClient::toolsChanged, this, [this, client]() {
        resyncClient(client);
    });

    connect(client, &QObject::destroyed, this, [this, client]() {
        auto it = m_bindings.find(client);
        if (it == m_bindings.end())
            return;
        for (const QString &id : std::as_const(it->toolIds))
            m_registry->removeTool(id);
        m_bindings.erase(it);
    });

    connect(client, &McpClient::disconnected, this, [this, client]() {
        auto it = m_bindings.find(client);
        if (it == m_bindings.end())
            return;
        emit serverDisconnected(it->name);
        if (it->autoReconnect && !m_stopping) {
            clearTools(it.value());
            scheduleReconnect(client);
        }
    });
}

void McpToolBinder::initializeClient(McpClient *client, bool isReconnect)
{
    QPointer<McpClient> guard(client);
    const QString name = m_bindings.value(client).name;

    (void)LLMQore::compat(client->connectAndInitialize(std::chrono::seconds(30)))
        .then(this, [guard](const InitializeResult &) {
            if (!guard)
                throw Rpc::TransportError(QStringLiteral("Client destroyed during initialize"));
            return guard->listTools();
        })
        .unwrap()
        .then(this, [this, guard, name](const QList<ToolInfo> &tools) {
            if (!guard || !m_bindings.contains(guard))
                return;
            Binding &binding = m_bindings[guard];
            binding.reconnectPending = false;
            binding.backoffMs = kInitialBackoffMs;
            applyTools(guard, tools);
            emit toolsSynced(name, tools.size());
            emit serverInitialized(name, guard->serverInfo());
        })
        .onFailed(this, [this, guard, name, isReconnect](const std::exception &e) {
            const QString error = QString::fromUtf8(e.what());
            qCWarning(llmMcpLog).noquote()
                << QString("MCP server '%1': initialize failed: %2").arg(name, error);
            emit serverInitFailed(name, error);
            if (!guard || !m_bindings.contains(guard))
                return;
            Binding &binding = m_bindings[guard];
            binding.reconnectPending = false;
            if (isReconnect && binding.autoReconnect && !m_stopping) {
                binding.backoffMs = std::min(binding.backoffMs * 2, kMaxBackoffMs);
                scheduleReconnect(guard);
            }
        });
}

void McpToolBinder::resyncClient(McpClient *client)
{
    if (!m_bindings.contains(client))
        return;
    QPointer<McpClient> guard(client);
    const QString name = m_bindings.value(client).name;

    (void)LLMQore::compat(client->listTools())
        .then(this, [this, guard, name](const QList<ToolInfo> &tools) {
            if (!guard || !m_bindings.contains(guard))
                return;
            applyTools(guard, tools);
            emit toolsSynced(name, tools.size());
        })
        .onFailed(this, [name](const std::exception &e) {
            qCWarning(llmMcpLog).noquote()
                << QString("MCP server '%1': tool listing failed: %2")
                       .arg(name, QString::fromUtf8(e.what()));
        });
}

void McpToolBinder::applyTools(McpClient *client, const QList<ToolInfo> &tools)
{
    Binding &binding = m_bindings[client];

    QSet<QString> incoming;
    incoming.reserve(tools.size());
    for (const ToolInfo &info : tools)
        incoming.insert(prefixedToolId(binding.name, info.name));

    for (const QString &old : std::as_const(binding.toolIds)) {
        if (!incoming.contains(old))
            m_registry->removeTool(old);
    }

    QStringList registered;
    registered.reserve(tools.size());
    for (const ToolInfo &info : tools) {
        const QString id = prefixedToolId(binding.name, info.name);
        if (m_registry->tool(id))
            m_registry->removeTool(id);
        m_registry->addTool(new McpRemoteTool(client, binding.name, info));
        registered.append(id);
    }
    binding.toolIds = std::move(registered);
}

void McpToolBinder::clearTools(Binding &binding)
{
    for (const QString &id : std::as_const(binding.toolIds))
        m_registry->removeTool(id);
    binding.toolIds.clear();
}

void McpToolBinder::scheduleReconnect(McpClient *client)
{
    auto it = m_bindings.find(client);
    if (it == m_bindings.end() || it->reconnectPending || m_stopping)
        return;
    it->reconnectPending = true;

    qCInfo(llmMcpLog).noquote()
        << QString("MCP server '%1': disconnected, reconnect in %2 ms")
               .arg(it->name)
               .arg(it->backoffMs);

    QPointer<McpClient> guard(client);
    QTimer::singleShot(it->backoffMs, this, [this, guard]() {
        if (m_stopping || !guard || !m_bindings.contains(guard))
            return;
        initializeClient(guard, /*isReconnect*/ true);
    });
}

} // namespace LLMQore::Mcp
