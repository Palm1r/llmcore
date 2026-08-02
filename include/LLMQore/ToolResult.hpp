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

#include <LLMQore/ContentBlocks.hpp>
#include <LLMQore/LLMQore_global.h>

namespace LLMQore {

LLMQORE_EXPORT QJsonObject toolContentToJson(const ToolContent &content);
LLMQORE_EXPORT ToolContent toolContentFromJson(const QJsonObject &obj);
LLMQORE_EXPORT QString toolContentAsText(const ToolContent &content);

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

    bool hasOnlyText() const;

    QJsonObject toJson() const;
    static ToolResult fromJson(const QJsonObject &obj);
};

LLMQORE_EXPORT QString toolResultText(const ToolResult &result);

LLMQORE_EXPORT ToolResult toToolResult(const ToolResultContent &content);
LLMQORE_EXPORT ToolResultContent makeToolResultContent(
    const QString &toolUseId, const QString &name, const ToolResult &result);

} // namespace LLMQore

using LLMQoreToolResultHash = QHash<QString, LLMQore::ToolResult>;

Q_DECLARE_METATYPE(LLMQore::ToolResult)
Q_DECLARE_METATYPE(LLMQoreToolResultHash)
