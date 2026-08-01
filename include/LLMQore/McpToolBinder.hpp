// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <LLMQore/LLMQore_global.h>
#include <LLMQore/McpProvisioning.hpp>
#include <LLMQore/McpTypes.hpp>
#include <LLMQore/Version.hpp>

namespace LLMQore {
class ToolRegistry;
}

namespace LLMQore::Mcp {

class McpClient;
struct ToolInfo;

// Keeps a ToolRegistry in sync with the tools of one or more MCP servers:
// connect, list, wrap each tool in a McpRemoteTool (id-prefixed with the
// server name so two servers exposing the same tool never collide),
// diff-resync on toolsChanged, reconnect with backoff for servers it
// provisioned itself, and removal of a client's tools when it goes away.
class LLMQORE_EXPORT McpToolBinder : public QObject
{
    Q_OBJECT
public:
    explicit McpToolBinder(LLMQore::ToolRegistry *registry, QObject *parent = nullptr);
    ~McpToolBinder() override;

    // Identity reported to servers by clients the binder creates itself.
    void setClientInfo(Implementation info);

    // Provisions a client+transport for `endpoint` (binder-owned, with
    // reconnect+backoff). Returns false when the endpoint names neither a
    // url nor a command.
    bool addServer(const ServerEndpoint &endpoint);

    // Provisions every endpoint of the `mcpServers` map in `config`.
    // Returns the number of servers accepted.
    int loadServers(const QJsonObject &config);

    // Binds an externally-owned, already-provisioned client. Its tool ids
    // are prefixed with `serverName` when given. The binder follows
    // toolsChanged, removes the tools when the client is destroyed, and —
    // only when `autoReconnect` is set — re-initializes it with backoff
    // after a transport loss.
    void addClient(
        McpClient *client, const QString &serverName = {}, bool autoReconnect = false);

    void removeClient(McpClient *client);

    // Stops reconnect attempts and shuts down binder-owned clients.
    void shutdown();

signals:
    void serverInitialized(const QString &name, const LLMQore::Mcp::InitializeResult &result);
    void serverInitFailed(const QString &name, const QString &error);
    void toolsSynced(const QString &name, int toolCount);
    void serverDisconnected(const QString &name);

private:
    struct Binding
    {
        QString name;
        QStringList toolIds;
        bool owned = false;
        bool autoReconnect = false;
        bool reconnectPending = false;
        int backoffMs = 0;
    };

    void attach(McpClient *client, const QString &name, bool owned, bool autoReconnect);
    void initializeClient(McpClient *client, bool isReconnect);
    void resyncClient(McpClient *client);
    void applyTools(McpClient *client, const QList<ToolInfo> &tools);
    void clearTools(Binding &binding);
    void scheduleReconnect(McpClient *client);

    LLMQore::ToolRegistry *m_registry = nullptr;
    Implementation m_clientInfo{"LLMQore", QStringLiteral(LLMQORE_VERSION_STRING)};
    QHash<McpClient *, Binding> m_bindings;
    bool m_stopping = false;
};

} // namespace LLMQore::Mcp
