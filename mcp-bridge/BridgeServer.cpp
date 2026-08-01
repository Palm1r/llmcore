// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "BridgeServer.hpp"

#include <QCoreApplication>

using namespace LLMQore;
using namespace LLMQore::Mcp;

namespace McpBridge {

BridgeServer::BridgeServer(const BridgeConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
    if (config.stdioMode) {
        m_serverTransport = new McpStdioServerTransport(this);
    } else {
        HttpServerConfig httpCfg;
        httpCfg.port = config.port;
        httpCfg.address = config.address;
        m_httpTransport = new McpHttpServerTransport(httpCfg, this);
        m_serverTransport = m_httpTransport;
    }

    McpServerConfig serverCfg;
    serverCfg.serverInfo = {QCoreApplication::applicationName(),
                            QCoreApplication::applicationVersion()};
    serverCfg.instructions = "MCP Bridge aggregating multiple upstream MCP servers.";

    m_server = new McpServer(m_serverTransport, serverCfg, this);
    m_registry = new ToolRegistry(this);
    m_server->setToolRegistry(m_registry);

    m_binder = new McpToolBinder(m_registry, this);
    m_binder->setClientInfo({QCoreApplication::applicationName(),
                             QCoreApplication::applicationVersion()});

    connect(m_binder, &McpToolBinder::serverInitialized, this,
            [this](const QString &name, const InitializeResult &result) {
                qInfo().noquote() << QString("[%1] connected — %2 %3")
                                         .arg(name,
                                              result.serverInfo.name,
                                              result.serverInfo.version);
                ++m_completedInits;
                checkAllReady();
            });

    connect(m_binder, &McpToolBinder::serverInitFailed, this,
            [this](const QString &name, const QString &error) {
                qWarning().noquote() << QString("[%1] init failed: %2").arg(name, error);
                ++m_completedInits;
                checkAllReady();
            });

    connect(m_binder, &McpToolBinder::toolsSynced, this,
            [](const QString &name, int toolCount) {
                qInfo().noquote()
                    << QString("[%1] synced: %2 tools.").arg(name).arg(toolCount);
            });

    connect(m_binder, &McpToolBinder::serverDisconnected, this,
            [](const QString &name) {
                qWarning().noquote() << QString("[%1] upstream disconnected.").arg(name);
            });
}

void BridgeServer::start()
{
    for (const ServerEndpoint &endpoint : std::as_const(m_config.upstreams)) {
        qInfo().noquote() << QString("Connecting to [%1]...").arg(endpoint.name);
        if (m_binder->addServer(endpoint))
            ++m_pendingInits;
    }

    if (m_pendingInits == 0)
        emit startFailed("No valid upstream servers to connect.");
}

void BridgeServer::shutdown()
{
    qInfo() << "Shutting down...";
    m_binder->shutdown();
    m_server->stop();
}

quint16 BridgeServer::serverPort() const
{
    return m_httpTransport ? m_httpTransport->serverPort() : 0;
}

void BridgeServer::checkAllReady()
{
    if (m_ready || m_completedInits < m_pendingInits)
        return;
    m_ready = true;

    m_server->start();

    QString url;
    if (m_httpTransport) {
        url = QString("http://%1:%2%3")
                  .arg(m_config.address.toString())
                  .arg(m_httpTransport->serverPort())
                  .arg(m_httpTransport->config().path);
        qInfo().noquote() << QString("MCP Bridge listening on %1").arg(url);
    } else {
        url = QStringLiteral("stdio");
        qInfo().noquote() << "MCP Bridge serving over stdio.";
    }

    qInfo().noquote() << QString("Aggregating %1 tools from %2 upstream servers.")
                             .arg(m_registry->registeredTools().size())
                             .arg(m_config.upstreams.size());

    emit ready(url);
}

} // namespace McpBridge
