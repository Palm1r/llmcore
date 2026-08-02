// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "BridgeConfig.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace McpBridge {

BridgeConfig loadConfig(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qCritical().noquote() << "Cannot open config:" << path;
        return {};
    }

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qCritical().noquote() << "Config parse error:" << err.errorString();
        return {};
    }

    const QJsonObject root = doc.object();
    const QJsonObject serversObj = root["mcpServers"].toObject();
    if (serversObj.isEmpty()) {
        qCritical() << "No servers defined in mcpServers.";
        return {};
    }

    BridgeConfig cfg;
    cfg.port = static_cast<quint16>(root["port"].toInt(8808));

    if (root.contains("host"))
        cfg.address = QHostAddress(root["host"].toString());

    // Servers that advertise a `/sse` URL usually speak the legacy
    // 2024-11-05 transport (split GET /sse + POST /messages). For those,
    // the user must write:
    //     "spec": "2024-11-05"
    // Without it, initialize round-trips to a spec the server doesn't
    // recognise and hangs until the client timeout.
    cfg.upstreams = LLMQore::Mcp::parseServerMap(root);

    return cfg;
}

} // namespace McpBridge
