// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/Conversation.hpp>

#include <QJsonArray>

namespace LLMQore {

namespace {

const int _conversationMetaType = []() {
    qRegisterMetaType<LLMQore::Turn>("LLMQore::Turn");
    qRegisterMetaType<LLMQore::Conversation>("LLMQore::Conversation");
    return 0;
}();

QString roleToString(TurnRole role)
{
    switch (role) {
    case TurnRole::User:
        return QStringLiteral("user");
    case TurnRole::Assistant:
        return QStringLiteral("assistant");
    case TurnRole::Tool:
        return QStringLiteral("tool");
    }
    return QStringLiteral("user");
}

TurnRole roleFromString(const QString &role)
{
    if (role == QLatin1String("assistant"))
        return TurnRole::Assistant;
    if (role == QLatin1String("tool"))
        return TurnRole::Tool;
    return TurnRole::User;
}

QJsonObject turnContentToJson(const TurnContent &block)
{
    return std::visit(
        overloaded{
            [](const TextContent &c) -> QJsonObject {
                return QJsonObject{{"type", "text"}, {"text", c.text}};
            },
            [](const ImageContent &c) -> QJsonObject {
                QJsonObject obj{{"type", "image"}};
                if (!c.mimeType.isEmpty())
                    obj.insert("mimeType", c.mimeType);
                if (c.isUrl())
                    obj.insert("url", c.url().toString());
                else
                    obj.insert("data", c.base64());
                return obj;
            },
            [](const AudioContent &c) -> QJsonObject {
                QJsonObject obj{
                    {"type", "audio"}, {"data", QString::fromUtf8(c.data.toBase64())}};
                if (!c.mimeType.isEmpty())
                    obj.insert("mimeType", c.mimeType);
                return obj;
            },
            [](const ToolUseContent &c) -> QJsonObject {
                return QJsonObject{
                    {"type", "tool_use"}, {"id", c.id}, {"name", c.name}, {"input", c.input}};
            },
            [](const ToolResultContent &c) -> QJsonObject {
                QJsonArray inner;
                for (const ToolContent &part : c.content)
                    inner.append(toolContentToJson(part));

                QJsonObject obj{
                    {"type", "tool_result"},
                    {"toolUseId", c.toolUseId},
                    {"name", c.name},
                    {"content", inner}};
                if (c.isError)
                    obj.insert("isError", true);
                if (!c.structuredContent.isEmpty())
                    obj.insert("structuredContent", c.structuredContent);
                return obj;
            },
            [](const ThinkingContent &c) -> QJsonObject {
                QJsonObject obj{{"type", "thinking"}, {"thinking", c.thinking}};
                if (!c.signature.isEmpty())
                    obj.insert("signature", c.signature);
                if (!c.itemId.isEmpty())
                    obj.insert("itemId", c.itemId);
                if (!c.encryptedContent.isEmpty())
                    obj.insert("encryptedContent", c.encryptedContent);
                return obj;
            },
            [](const RedactedThinkingContent &c) -> QJsonObject {
                QJsonObject obj{{"type", "redacted_thinking"}};
                if (!c.signature.isEmpty())
                    obj.insert("signature", c.signature);
                return obj;
            }},
        block);
}

TurnContent turnContentFromJson(const QJsonObject &obj)
{
    const QString type = obj.value("type").toString();

    if (type == QLatin1String("image")) {
        if (obj.contains("url"))
            return ImageContent::fromUrl(
                QUrl(obj.value("url").toString()), obj.value("mimeType").toString());
        return ImageContent::fromBase64(
            obj.value("data").toString(), obj.value("mimeType").toString());
    }

    if (type == QLatin1String("audio")) {
        return AudioContent{
            QByteArray::fromBase64(obj.value("data").toString().toUtf8()),
            obj.value("mimeType").toString()};
    }

    if (type == QLatin1String("tool_use")) {
        return ToolUseContent{
            obj.value("id").toString(),
            obj.value("name").toString(),
            obj.value("input").toObject()};
    }

    if (type == QLatin1String("tool_result")) {
        ToolResultContent result;
        result.toolUseId = obj.value("toolUseId").toString();
        result.name = obj.value("name").toString();
        result.isError = obj.value("isError").toBool();
        result.structuredContent = obj.value("structuredContent").toObject();
        const QJsonArray inner = obj.value("content").toArray();
        for (const QJsonValue &value : inner)
            result.content.append(toolContentFromJson(value.toObject()));
        return result;
    }

    if (type == QLatin1String("thinking")) {
        ThinkingContent thinking;
        thinking.thinking = obj.value("thinking").toString();
        thinking.signature = obj.value("signature").toString();
        thinking.itemId = obj.value("itemId").toString();
        thinking.encryptedContent = obj.value("encryptedContent").toString();
        return thinking;
    }

    if (type == QLatin1String("redacted_thinking"))
        return RedactedThinkingContent{obj.value("signature").toString(), false};

    return TextContent{obj.value("text").toString()};
}

} // namespace

QString Turn::text() const
{
    QString out;
    for (const TurnContent &block : content) {
        if (const auto *text = std::get_if<TextContent>(&block))
            out += text->text;
    }
    return out;
}

void Conversation::setSystem(const QString &system)
{
    m_system = system;
}

QString Conversation::system() const
{
    return m_system;
}

void Conversation::addUser(const QString &text)
{
    m_turns.append(Turn{TurnRole::User, {TextContent{text}}});
}

void Conversation::addUser(QList<TurnContent> content)
{
    m_turns.append(Turn{TurnRole::User, std::move(content)});
}

void Conversation::addAssistant(const QString &text)
{
    m_turns.append(Turn{TurnRole::Assistant, {TextContent{text}}});
}

void Conversation::addAssistant(QList<TurnContent> content)
{
    m_turns.append(Turn{TurnRole::Assistant, std::move(content)});
}

void Conversation::addToolResults(const QList<ToolResultContent> &results)
{
    if (results.isEmpty())
        return;

    Turn turn;
    turn.role = TurnRole::Tool;
    turn.content.reserve(results.size());
    for (const ToolResultContent &result : results)
        turn.content.append(result);
    m_turns.append(std::move(turn));
}

void Conversation::addTurn(Turn turn)
{
    m_turns.append(std::move(turn));
}

const QList<Turn> &Conversation::turns() const noexcept
{
    return m_turns;
}

bool Conversation::isEmpty() const noexcept
{
    return m_turns.isEmpty();
}

void Conversation::clear()
{
    m_system.clear();
    m_turns.clear();
}

QJsonObject Conversation::toJson() const
{
    QJsonArray turns;
    for (const Turn &turn : m_turns) {
        QJsonArray content;
        for (const TurnContent &block : turn.content)
            content.append(turnContentToJson(block));
        turns.append(QJsonObject{{"role", roleToString(turn.role)}, {"content", content}});
    }

    QJsonObject obj{{"turns", turns}};
    if (!m_system.isEmpty())
        obj.insert("system", m_system);
    return obj;
}

Conversation Conversation::fromJson(const QJsonObject &obj)
{
    Conversation conversation;
    conversation.m_system = obj.value("system").toString();

    const QJsonArray turns = obj.value("turns").toArray();
    for (const QJsonValue &turnValue : turns) {
        const QJsonObject turnObj = turnValue.toObject();

        Turn turn;
        turn.role = roleFromString(turnObj.value("role").toString());

        const QJsonArray content = turnObj.value("content").toArray();
        for (const QJsonValue &blockValue : content)
            turn.content.append(turnContentFromJson(blockValue.toObject()));

        conversation.m_turns.append(std::move(turn));
    }

    return conversation;
}

ToolResult toToolResult(const ToolResultContent &content)
{
    ToolResult result;
    result.content = content.content;
    result.isError = content.isError;
    result.structuredContent = content.structuredContent;
    return result;
}

ToolResultContent makeToolResultContent(
    const QString &toolUseId, const QString &name, const ToolResult &result)
{
    ToolResultContent content;
    content.toolUseId = toolUseId;
    content.name = name;
    content.content = result.content;
    content.isError = result.isError;
    content.structuredContent = result.structuredContent;
    return content;
}

} // namespace LLMQore
