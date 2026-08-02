// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseTool.hpp>
#include "ClaudeMessage.hpp"

#include <QJsonArray>
#include <QJsonDocument>

#include <LLMQore/Conversation.hpp>
#include <LLMQore/Log.hpp>

namespace LLMQore {

namespace {

class ClaudeToolDialect : public ToolDialect
{
public:
    QJsonObject wrapDefinition(const BaseTool &tool) const override
    {
        return QJsonObject{
            {"name", tool.id()},
            {"description", tool.description()},
            {"input_schema", tool.parametersSchema()}};
    }
};

} // namespace

const ToolDialect &ClaudeMessage::toolDialect()
{
    static const ClaudeToolDialect dialect;
    return dialect;
}


ClaudeMessage::ClaudeMessage(QObject *parent)
    : BaseMessage(parent)
{}

void ClaudeMessage::handleContentBlockStart(
    int index, const QString &blockType, const QJsonObject &data)
{
    qCDebug(llmClaudeLog).noquote()
        << QString("handleContentBlockStart index=%1, blockType=%2").arg(index).arg(blockType);

    if (blockType == "text") {
        addCurrentContent(TextContent{});

    } else if (blockType == "image") {
        const QJsonObject source = data["source"].toObject();
        const QString sourceType = source["type"].toString();

        if (sourceType == "url") {
            addCurrentContent(ImageContent::fromUrl(QUrl(source["url"].toString())));
        } else {
            addCurrentContent(ImageContent::fromBase64(
                source["data"].toString(), source["media_type"].toString()));
        }

    } else if (blockType == "tool_use") {
        addCurrentContent(ToolUseContent{
            data["id"].toString(), data["name"].toString(), data["input"].toObject()});
        m_pendingToolInputs[index] = "";

    } else if (blockType == "thinking") {
        const QString signature = data["signature"].toString();
        qCDebug(llmClaudeLog).noquote()
            << QString("Creating thinking block with signature length=%1").arg(signature.length());
        addCurrentContent(ThinkingContent{data["thinking"].toString(), signature, {}, {}, false});

    } else if (blockType == "redacted_thinking") {
        const QString signature = data["signature"].toString();
        qCDebug(llmClaudeLog).noquote()
            << QString("Creating redacted_thinking block with signature length=%1")
                   .arg(signature.length());
        addCurrentContent(RedactedThinkingContent{signature, false});
    }
}

void ClaudeMessage::handleContentBlockDelta(
    int index, const QString &deltaType, const QJsonObject &delta)
{
    if (index >= m_currentBlocks.size()) {
        return;
    }

    if (deltaType == "text_delta") {
        if (auto *textContent = blockAt<TextContent>(index))
            textContent->text += delta["text"].toString();

    } else if (deltaType == "input_json_delta") {
        QString partialJson = delta["partial_json"].toString();
        if (m_pendingToolInputs.contains(index)) {
            m_pendingToolInputs[index] += partialJson;
        }

    } else if (deltaType == "thinking_delta") {
        if (auto *thinkingContent = blockAt<ThinkingContent>(index))
            thinkingContent->thinking += delta["thinking"].toString();

    } else if (deltaType == "signature_delta") {
        const QString signature = delta["signature"].toString();
        if (auto *thinkingContent = blockAt<ThinkingContent>(index)) {
            thinkingContent->signature = signature;
            qCDebug(llmClaudeLog).noquote()
                << QString("Set signature for thinking block %1: length=%2")
                       .arg(index)
                       .arg(signature.length());
        } else if (auto *redactedContent = blockAt<RedactedThinkingContent>(index)) {
            redactedContent->signature = signature;
            qCDebug(llmClaudeLog).noquote()
                << QString("Set signature for redacted_thinking block %1: length=%2")
                       .arg(index)
                       .arg(signature.length());
        }
    }
}

void ClaudeMessage::handleContentBlockStop(int index)
{
    if (m_pendingToolInputs.contains(index)) {
        QString jsonInput = m_pendingToolInputs[index];
        QJsonObject inputObject;

        if (!jsonInput.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(jsonInput.toUtf8());
            if (doc.isObject()) {
                inputObject = doc.object();
            }
        }

        if (auto *toolContent = blockAt<ToolUseContent>(index))
            toolContent->input = inputObject;

        m_pendingToolInputs.remove(index);
    }
}

void ClaudeMessage::handleStopReason(const QString &stopReason)
{
    m_stopReason = stopReason;
    updateStateFromStopReason();
}

namespace {

QJsonObject toClaudeInnerBlock(const ToolContent &block)
{
    return std::visit(
        overloaded{
            [](const TextContent &c) -> QJsonObject {
                return QJsonObject{{"type", "text"}, {"text", c.text}};
            },
            [](const ImageContent &c) -> QJsonObject {
                if (c.isUrl()) {
                    return QJsonObject{
                        {"type", "image"},
                        {"source", QJsonObject{{"type", "url"}, {"url", c.url().toString()}}}};
                }
                const QString mime = c.mimeType.isEmpty() ? QStringLiteral("image/png")
                                                          : c.mimeType;
                return QJsonObject{
                    {"type", "image"},
                    {"source",
                     QJsonObject{
                         {"type", "base64"}, {"media_type", mime}, {"data", c.base64()}}}};
            },
            [](const AudioContent &c) -> QJsonObject {
                return QJsonObject{
                    {"type", "text"},
                    {"text",
                     QString("[audio: %1]")
                         .arg(c.mimeType.isEmpty() ? QStringLiteral("unknown") : c.mimeType)}};
            },
            [](const ResourceContent &c) -> QJsonObject {
                if (!c.isBlob() && !c.text().isEmpty())
                    return QJsonObject{{"type", "text"}, {"text", c.text()}};
                return QJsonObject{
                    {"type", "text"}, {"text", QString("[resource: %1]").arg(c.uri)}};
            },
            [](const ResourceLinkContent &c) -> QJsonObject {
                return QJsonObject{
                    {"type", "text"}, {"text", QString("[resource link: %1]").arg(c.uri)}};
            }},
        block);
}

QJsonValue buildClaudeToolResultContent(const ToolResult &r)
{
    if (r.content.isEmpty())
        return QString();
    if (r.content.size() == 1) {
        if (const auto *text = std::get_if<TextContent>(&r.content.first()))
            return text->text;
    }

    QJsonArray arr;
    for (const ToolContent &block : r.content)
        arr.append(toClaudeInnerBlock(block));
    return arr;
}

} // namespace

QJsonValue ClaudeMessage::serializeTurnContent(const TurnContent &block)
{
    return std::visit(
        overloaded{
            [](const TextContent &c) -> QJsonValue {
                return QJsonObject{{"type", "text"}, {"text", c.text}};
            },
            [](const ImageContent &c) -> QJsonValue {
                QJsonObject source;
                if (c.isUrl()) {
                    source["type"] = "url";
                    source["url"] = c.url().toString();
                } else {
                    source["type"] = "base64";
                    source["media_type"] = c.mimeType.isEmpty() ? QStringLiteral("image/png")
                                                                : c.mimeType;
                    source["data"] = c.base64();
                }
                return QJsonObject{{"type", "image"}, {"source", source}};
            },
            [](const AudioContent &c) -> QJsonValue {
                return QJsonObject{
                    {"type", "text"},
                    {"text",
                     QString("[audio: %1]")
                         .arg(c.mimeType.isEmpty() ? QStringLiteral("unknown") : c.mimeType)}};
            },
            [](const ToolUseContent &c) -> QJsonValue {
                return QJsonObject{
                    {"type", "tool_use"}, {"id", c.id}, {"name", c.name}, {"input", c.input}};
            },
            [](const ToolResultContent &c) -> QJsonValue {
                QJsonObject block{
                    {"type", "tool_result"},
                    {"tool_use_id", c.toolUseId},
                    {"content", buildClaudeToolResultContent(toToolResult(c))}};
                if (c.isError)
                    block.insert("is_error", true);
                return block;
            },
            [](const ThinkingContent &c) -> QJsonValue {
                QJsonObject obj{{"type", "thinking"}, {"thinking", c.thinking}};
                if (!c.signature.isEmpty())
                    obj["signature"] = c.signature;
                return obj;
            },
            [](const RedactedThinkingContent &c) -> QJsonValue {
                QJsonObject obj{{"type", "redacted_thinking"}};
                if (!c.signature.isEmpty())
                    obj["signature"] = c.signature;
                return obj;
            }},
        block);
}

QJsonObject ClaudeMessage::toProviderFormat() const
{
    QJsonObject message;
    message["role"] = "assistant";

    QJsonArray content;

    for (const TurnContent &block : m_currentBlocks) {
        content.append(serializeTurnContent(block));
    }

    message["content"] = content;

    qCDebug(llmClaudeLog).noquote()
        << QString("toProviderFormat - message with %1 content block(s)").arg(m_currentBlocks.size());

    return message;
}


QJsonArray ClaudeMessage::createToolResultsContent(
    const QHash<QString, ToolResult> &toolResults) const
{
    return mapToolResults(
        toolResults, [](const ToolUseContent &use, const ToolResult &r, QJsonArray &out) {
            QJsonObject block{
                {"type", "tool_result"},
                {"tool_use_id", use.id},
                {"content", buildClaudeToolResultContent(r)},
            };
            if (r.isError)
                block.insert("is_error", true);
            out.append(block);
        });
}

QList<RedactedThinkingContent> ClaudeMessage::getCurrentRedactedThinkingContent() const
{
    QList<RedactedThinkingContent> redactedBlocks;
    for (const TurnContent &block : m_currentBlocks) {
        if (const auto *redactedContent = std::get_if<RedactedThinkingContent>(&block))
            redactedBlocks.append(*redactedContent);
    }
    return redactedBlocks;
}

void ClaudeMessage::startNewContinuation()
{
    qCDebug(llmClaudeLog).noquote() << "Starting new continuation";

    BaseMessage::startNewContinuation();
    m_pendingToolInputs.clear();
    m_stopReason.clear();
}

void ClaudeMessage::updateStateFromStopReason()
{
    if (m_stopReason == "tool_use" && !getCurrentToolUseContent().empty()) {
        m_state = MessageState::RequiresToolExecution;
    } else if (m_stopReason == "end_turn") {
        m_state = MessageState::Final;
    } else {
        m_state = MessageState::Complete;
    }
}

} // namespace LLMQore
