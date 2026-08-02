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

[[nodiscard]] LLMQORE_EXPORT QList<ServerEndpoint> parseServerMap(const QJsonObject &config);

[[nodiscard]] LLMQORE_EXPORT McpHttpSpec parseHttpSpec(const QString &spec);

[[nodiscard]] LLMQORE_EXPORT Rpc::Transport *makeTransport(
    const ServerEndpoint &endpoint, QObject *parent);

} // namespace LLMQore::Mcp
