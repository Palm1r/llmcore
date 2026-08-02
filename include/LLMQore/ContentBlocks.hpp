// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <variant>

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QUrl>

namespace LLMQore {

enum class MessageState { Building, Complete, RequiresToolExecution, Final };

struct TextContent
{
    QString text;
};

struct ImageContent
{
    std::variant<QByteArray, QUrl> source;
    QString mimeType;

    static ImageContent fromBytes(const QByteArray &bytes, const QString &mimeType = {})
    {
        return ImageContent{bytes, mimeType};
    }
    static ImageContent fromBase64(const QString &base64, const QString &mimeType = {})
    {
        return ImageContent{QByteArray::fromBase64(base64.toUtf8()), mimeType};
    }
    static ImageContent fromUrl(const QUrl &url, const QString &mimeType = {})
    {
        return ImageContent{url, mimeType};
    }

    bool isUrl() const noexcept { return std::holds_alternative<QUrl>(source); }

    QByteArray bytes() const
    {
        const auto *data = std::get_if<QByteArray>(&source);
        return data ? *data : QByteArray{};
    }
    QString base64() const { return QString::fromUtf8(bytes().toBase64()); }
    QUrl url() const
    {
        const auto *link = std::get_if<QUrl>(&source);
        return link ? *link : QUrl{};
    }
};

struct AudioContent
{
    QByteArray data;
    QString mimeType;
};

struct ToolUseContent
{
    QString id;
    QString name;
    QJsonObject input;
};

struct ThinkingContent
{
    QString thinking;
    QString signature;
    QString itemId;
    QString encryptedContent;
    bool notified = false;
};

struct RedactedThinkingContent
{
    QString signature;
    bool notified = false;
};

struct ResourceContent
{
    QString uri;
    std::variant<QString, QByteArray> contents;
    QString mimeType;

    static ResourceContent fromText(
        const QString &uri, const QString &text, const QString &mimeType = {})
    {
        return ResourceContent{uri, text, mimeType};
    }
    static ResourceContent fromBlob(
        const QString &uri, const QByteArray &blob, const QString &mimeType = {})
    {
        return ResourceContent{uri, blob, mimeType};
    }

    bool isBlob() const noexcept { return std::holds_alternative<QByteArray>(contents); }

    QString text() const
    {
        const auto *value = std::get_if<QString>(&contents);
        return value ? *value : QString{};
    }
    QByteArray blob() const
    {
        const auto *value = std::get_if<QByteArray>(&contents);
        return value ? *value : QByteArray{};
    }
};

struct ResourceLinkContent
{
    QString uri;
    QString name;
    QString description;
    QString mimeType;
};

using ToolContent = std::
    variant<TextContent, ImageContent, AudioContent, ResourceContent, ResourceLinkContent>;

struct ToolResultContent
{
    QString toolUseId;
    QString name;
    QList<ToolContent> content;
    bool isError = false;
    QJsonObject structuredContent;
};

using TurnContent = std::variant<
    TextContent,
    ImageContent,
    AudioContent,
    ToolUseContent,
    ToolResultContent,
    ThinkingContent,
    RedactedThinkingContent>;

template<typename... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};
template<typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

} // namespace LLMQore

Q_DECLARE_METATYPE(LLMQore::TurnContent)
Q_DECLARE_METATYPE(LLMQore::ToolContent)
