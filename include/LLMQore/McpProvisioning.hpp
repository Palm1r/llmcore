// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <LLMQore/LLMQore_global.h>
#include <LLMQore/McpHttpTransport.hpp>

namespace LLMQore::Rpc {
class Transport;
}

namespace LLMQore::Mcp {

// Where one MCP server lives. An HTTP endpoint wins over a stdio command when
// both are set. Shared by every host that provisions upstream servers -- the
// tool manager and the bridge -- so the wire spellings are decided once.
struct LLMQORE_EXPORT ServerEndpoint
{
    QString name;

    QUrl url;
    QHash<QString, QString> headers;
    QString httpSpec;

    QString command;
    QStringList arguments;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString workingDirectory;

    [[nodiscard]] bool hasHttpEndpoint() const
    {
        const QString scheme = url.scheme();
        return (scheme == QLatin1String("http") || scheme == QLatin1String("https"))
               && url.isValid();
    }

    [[nodiscard]] bool isValid() const
    {
        return hasHttpEndpoint() || !command.isEmpty();
    }

    [[nodiscard]] static ServerEndpoint fromJson(const QString &name, const QJsonObject &entry);
};

// Parses the `mcpServers` map of `config` into endpoints, skipping entries
// that are disabled (`"enable": false`) or name neither a url nor a command.
[[nodiscard]] LLMQORE_EXPORT QList<ServerEndpoint> parseServerMap(const QJsonObject &config);

// "2024-11-05" selects the legacy two-channel SSE spec; every other known
// spelling, and the empty string, select the latest.
[[nodiscard]] LLMQORE_EXPORT McpHttpSpec parseHttpSpec(const QString &spec);

// The transport `endpoint` describes, parented to `parent`, or nullptr when it
// names neither a URL nor a command.
[[nodiscard]] LLMQORE_EXPORT Rpc::Transport *makeTransport(
    const ServerEndpoint &endpoint, QObject *parent);

} // namespace LLMQore::Mcp
