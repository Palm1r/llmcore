// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>

#include <LLMQore/LLMQore_global.h>

namespace LLMQore {

struct LLMQORE_EXPORT ToolContent
{
    enum Type {
        Text,
        Image,
        Audio,
        Resource,
        ResourceLink,
    };

    Type type = Text;

    QString text;
    QByteArray data;
    QString mimeType;
    QString uri;

    QString resourceText;
    QByteArray resourceBlob;

    QString name;
    QString description;

    static ToolContent makeText(const QString &text);
    static ToolContent makeImage(const QByteArray &data, const QString &mimeType);
    static ToolContent makeAudio(const QByteArray &data, const QString &mimeType);
    static ToolContent makeResourceText(
        const QString &uri, const QString &text, const QString &mimeType = {});
    static ToolContent makeResourceBlob(
        const QString &uri, const QByteArray &blob, const QString &mimeType = {});
    static ToolContent makeResourceLink(
        const QString &uri,
        const QString &name = {},
        const QString &description = {},
        const QString &mimeType = {});

    QJsonObject toJson() const;
    static ToolContent fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT ToolResult
{
    QList<ToolContent> content;
    bool isError = false;
    QJsonObject structuredContent;

    static ToolResult text(const QString &text);
    static ToolResult error(const QString &message);
    static ToolResult empty();

    QString asText() const;
    bool isEmpty() const;

    QJsonObject toJson() const;
    static ToolResult fromJson(const QJsonObject &obj);
};

} // namespace LLMQore

using LLMQoreToolResultHash = QHash<QString, LLMQore::ToolResult>;

Q_DECLARE_METATYPE(LLMQore::ToolContent)
Q_DECLARE_METATYPE(LLMQore::ToolResult)
Q_DECLARE_METATYPE(LLMQoreToolResultHash)
