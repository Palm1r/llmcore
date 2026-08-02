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
class McpRemoteTool;
struct ToolInfo;

class LLMQORE_EXPORT McpToolBinder : public QObject
{
    Q_OBJECT
public:
    explicit McpToolBinder(LLMQore::ToolRegistry *registry, QObject *parent = nullptr);
    ~McpToolBinder() override;

    void setClientInfo(Implementation info);

    bool addServer(const ServerEndpoint &endpoint);

    int loadServers(const QJsonObject &config);

    void addClient(
        McpClient *client, const QString &serverName = {}, bool autoReconnect = false);

    void removeClient(McpClient *client);

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
        QList<QPointer<McpRemoteTool>> tools;
        bool owned = false;
        bool autoReconnect = false;
        bool reconnectPending = false;
        int backoffMs = 0;
    };

    void attach(McpClient *client, const QString &name, bool owned, bool autoReconnect);
    void initializeClient(McpClient *client);
    void resyncClient(McpClient *client);
    void applyTools(McpClient *client, const QList<ToolInfo> &tools);
    void clearTools(Binding &binding);
    void scheduleReconnect(McpClient *client);

    QPointer<LLMQore::ToolRegistry> m_registry;
    Implementation m_clientInfo = {"LLMQore", QStringLiteral(LLMQORE_VERSION_STRING)};
    QHash<McpClient *, Binding> m_bindings;
    bool m_stopping = false;
};

} // namespace LLMQore::Mcp
