// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/ToolResult.hpp>

#include <QJsonValue>
#include <QMetaType>
#include <QStringList>

namespace LLMQore {

namespace {
const int _toolResultMetaType = []() {
    qRegisterMetaType<LLMQore::ToolContent>("LLMQore::ToolContent");
    qRegisterMetaType<LLMQore::TurnContent>("LLMQore::TurnContent");
    qRegisterMetaType<LLMQore::ToolResult>("LLMQore::ToolResult");
    qRegisterMetaType<LLMQoreToolResultHash>("LLMQoreToolResultHash");
    qRegisterMetaType<LLMQoreToolResultHash>("QHash<QString,ToolResult>");
    return 0;
}();
} // namespace

QJsonObject toolContentToJson(const ToolContent &content)
{
    return std::visit(
        overloaded{
            [](const TextContent &c) -> QJsonObject {
                return QJsonObject{{"type", "text"}, {"text", c.text}};
            },
            [](const ImageContent &c) -> QJsonObject {
                if (c.isUrl()) {
                    QJsonObject obj{{"type", "resource_link"}, {"uri", c.url().toString()}};
                    if (!c.mimeType.isEmpty())
                        obj.insert("mimeType", c.mimeType);
                    return obj;
                }
                QJsonObject obj{{"type", "image"}, {"data", c.base64()}};
                if (!c.mimeType.isEmpty())
                    obj.insert("mimeType", c.mimeType);
                return obj;
            },
            [](const AudioContent &c) -> QJsonObject {
                QJsonObject obj{
                    {"type", "audio"}, {"data", QString::fromUtf8(c.data.toBase64())}};
                if (!c.mimeType.isEmpty())
                    obj.insert("mimeType", c.mimeType);
                return obj;
            },
            [](const ResourceContent &c) -> QJsonObject {
                QJsonObject resource{{"uri", c.uri}};
                if (c.isBlob())
                    resource.insert("blob", QString::fromUtf8(c.blob().toBase64()));
                else if (!c.text().isEmpty())
                    resource.insert("text", c.text());
                if (!c.mimeType.isEmpty())
                    resource.insert("mimeType", c.mimeType);
                return QJsonObject{{"type", "resource"}, {"resource", resource}};
            },
            [](const ResourceLinkContent &c) -> QJsonObject {
                QJsonObject obj{{"type", "resource_link"}, {"uri", c.uri}};
                if (!c.name.isEmpty())
                    obj.insert("name", c.name);
                if (!c.description.isEmpty())
                    obj.insert("description", c.description);
                if (!c.mimeType.isEmpty())
                    obj.insert("mimeType", c.mimeType);
                return obj;
            }},
        content);
}

ToolContent toolContentFromJson(const QJsonObject &obj)
{
    const QString type = obj.value("type").toString();

    if (type == QLatin1String("text"))
        return TextContent{obj.value("text").toString()};

    if (type == QLatin1String("image")) {
        return ImageContent::fromBase64(
            obj.value("data").toString(), obj.value("mimeType").toString());
    }

    if (type == QLatin1String("audio")) {
        return AudioContent{
            QByteArray::fromBase64(obj.value("data").toString().toUtf8()),
            obj.value("mimeType").toString()};
    }

    if (type == QLatin1String("resource")) {
        const QJsonObject res = obj.value("resource").toObject();
        const QString uri = res.value("uri").toString();
        const QString mimeType = res.value("mimeType").toString();
        if (res.contains("blob")) {
            return ResourceContent::fromBlob(
                uri, QByteArray::fromBase64(res.value("blob").toString().toUtf8()), mimeType);
        }
        return ResourceContent::fromText(uri, res.value("text").toString(), mimeType);
    }

    if (type == QLatin1String("resource_link")) {
        return ResourceLinkContent{
            obj.value("uri").toString(),
            obj.value("name").toString(),
            obj.value("description").toString(),
            obj.value("mimeType").toString()};
    }

    return TextContent{QString("[unsupported content type: %1]").arg(type)};
}

QString toolContentAsText(const ToolContent &content)
{
    return std::visit(
        overloaded{
            [](const TextContent &c) -> QString { return c.text; },
            [](const ImageContent &c) -> QString {
                return QString("[image: %1]")
                    .arg(c.mimeType.isEmpty() ? QStringLiteral("unknown") : c.mimeType);
            },
            [](const AudioContent &c) -> QString {
                return QString("[audio: %1]")
                    .arg(c.mimeType.isEmpty() ? QStringLiteral("unknown") : c.mimeType);
            },
            [](const ResourceContent &c) -> QString {
                if (!c.isBlob() && !c.text().isEmpty())
                    return c.text();
                return QString("[resource: %1]").arg(c.uri);
            },
            [](const ResourceLinkContent &c) -> QString {
                return QString("[resource link: %1]").arg(c.uri);
            }},
        content);
}

ToolResult ToolResult::text(const QString &text)
{
    ToolResult r;
    r.content.append(TextContent{text});
    return r;
}

ToolResult ToolResult::error(const QString &message)
{
    ToolResult r;
    r.content.append(TextContent{message});
    r.isError = true;
    return r;
}

ToolResult ToolResult::empty()
{
    return ToolResult{};
}

QString ToolResult::asText() const
{
    QStringList parts;
    parts.reserve(content.size());
    for (const ToolContent &block : content)
        parts.append(toolContentAsText(block));
    return parts.join(QLatin1Char('\n'));
}

bool ToolResult::hasOnlyText() const
{
    for (const ToolContent &block : content) {
        if (!std::holds_alternative<TextContent>(block))
            return false;
    }
    return true;
}

bool ToolResult::isEmpty() const
{
    return content.isEmpty() && structuredContent.isEmpty();
}

QJsonObject ToolResult::toJson() const
{
    QJsonArray arr;
    for (const ToolContent &block : content)
        arr.append(toolContentToJson(block));

    QJsonObject obj{{"content", arr}};
    if (isError)
        obj.insert("isError", true);
    if (!structuredContent.isEmpty())
        obj.insert("structuredContent", structuredContent);
    return obj;
}

ToolResult ToolResult::fromJson(const QJsonObject &obj)
{
    ToolResult r;
    const QJsonArray arr = obj.value("content").toArray();
    for (const QJsonValue &v : arr)
        r.content.append(toolContentFromJson(v.toObject()));
    r.isError = obj.value("isError").toBool();
    r.structuredContent = obj.value("structuredContent").toObject();
    return r;
}

} // namespace LLMQore
