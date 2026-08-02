// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>

#include <LLMQore/Mcp>
#include <LLMQore/ToolRegistry.hpp>

#include "BridgeConfig.hpp"

namespace McpBridge {

class BridgeServer : public QObject
{
    Q_OBJECT
public:
    explicit BridgeServer(const BridgeConfig &config, QObject *parent = nullptr);

    void start();
    void shutdown();

    quint16 serverPort() const;

signals:
    void ready(const QString &url);
    void startFailed(const QString &reason);

private:
    void checkAllReady();

    BridgeConfig m_config;
    LLMQore::Rpc::Transport *m_serverTransport = nullptr;
    LLMQore::Mcp::McpHttpServerTransport *m_httpTransport = nullptr;
    LLMQore::Mcp::McpServer *m_server = nullptr;
    LLMQore::ToolRegistry *m_registry = nullptr;
    LLMQore::Mcp::McpToolBinder *m_binder = nullptr;
    int m_pendingInits = 0;
    int m_completedInits = 0;
    bool m_ready = false;
};

} // namespace McpBridge
