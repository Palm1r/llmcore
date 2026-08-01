// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <LLMQore/McpProvisioning.hpp>

namespace McpBridge {

struct BridgeConfig
{
    quint16 port = 8808;
    QHostAddress address = QHostAddress::LocalHost;
    bool stdioMode = false;
    QList<LLMQore::Mcp::ServerEndpoint> upstreams;
};

BridgeConfig loadConfig(const QString &path);

} // namespace McpBridge
