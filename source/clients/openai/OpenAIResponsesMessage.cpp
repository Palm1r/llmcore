// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseTool.hpp>
#include "OpenAIResponsesMessage.hpp"

#include <LLMQore/Log.hpp>

#include <QJsonArray>

namespace LLMQore {

namespace {

class OpenAIResponsesToolDialect : public ToolDialect
{
public:
    QJsonObject wrapDefinition(const BaseTool &tool) const override
    {
        return QJsonObject{
            {"type", "function"},
            {"name", tool.id()},
            {"description", tool.description()},
            {"parameters", tool.parametersSchema()}};
    }
};

} // namespace

const ToolDialect &OpenAIResponsesMessage::toolDialect()
{
    static const OpenAIResponsesToolDialect dialect;
    return dialect;
}


OpenAIResponsesMessage::OpenAIResponsesMessage(QObject *parent)
    : BaseMessage(parent)
{}

void OpenAIResponsesMessage::handleContentDelta(const QString &text)
{
    if (!text.isEmpty()) {
        const int index = getOrCreateTextItemIndex();
        if (auto *textItem = blockAt<TextContent>(index))
            textItem->text += text;
    }
}

void OpenAIResponsesMessage::handleToolCallStart(const QString &callId, const QString &name)
{
    m_toolCalls[callId] = addCurrentContent(ToolUseContent{callId, name, {}});
    m_pendingToolArguments[callId] = "";
}

void OpenAIResponsesMessage::handleToolCallDelta(const QString &callId, const QString &argumentsDelta)
{
    if (m_pendingToolArguments.contains(callId)) {
        m_pendingToolArguments[callId] += argumentsDelta;
    }
}

void OpenAIResponsesMessage::handleToolCallComplete(
    const QString &callId, const QString &finalArguments)
{
    if (m_pendingToolArguments.contains(callId) && m_toolCalls.contains(callId)) {
        const QString jsonArgs = !finalArguments.isEmpty() ? finalArguments
                                                           : m_pendingToolArguments[callId];
        QJsonObject argsObject;

        if (!jsonArgs.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(jsonArgs.toUtf8());
            if (doc.isObject()) {
                argsObject = doc.object();
            }
        }

        if (auto *toolContent = blockAt<ToolUseContent>(m_toolCalls.value(callId, -1)))
            toolContent->input = argsObject;
        m_pendingToolArguments.remove(callId);
    }
}

void OpenAIResponsesMessage::handleReasoningStart(const QString &itemId)
{
    ThinkingContent content;
    content.itemId = itemId;
    m_thinkingBlocks[itemId] = addCurrentContent(std::move(content));
}

void OpenAIResponsesMessage::handleReasoningEncryptedContent(
    const QString &itemId, const QString &encryptedContent)
{
    if (encryptedContent.isEmpty())
        return;

    if (!m_thinkingBlocks.contains(itemId))
        handleReasoningStart(itemId);

    if (auto *thinking = blockAt<ThinkingContent>(m_thinkingBlocks.value(itemId, -1)))
        thinking->encryptedContent = encryptedContent;
}

void OpenAIResponsesMessage::handleReasoningDelta(const QString &itemId, const QString &text)
{
    if (auto *thinking = blockAt<ThinkingContent>(m_thinkingBlocks.value(itemId, -1)))
        thinking->thinking += text;
}

void OpenAIResponsesMessage::handleReasoningComplete(const QString &itemId)
{
    Q_UNUSED(itemId);
}

void OpenAIResponsesMessage::handleStatus(const QString &status)
{
    m_status = status;
    updateStateFromStatus();
}

QList<QJsonObject> OpenAIResponsesMessage::toItemsFormat(bool includeReasoning) const
{
    QList<QJsonObject> items;

    QString textContent;
    int textPosition = -1;

    for (const TurnContent &block : m_currentBlocks) {
        std::visit(
            overloaded{
                [&](const TextContent &c) {
                    if (textPosition < 0)
                        textPosition = items.size();
                    textContent += c.text;
                },
                [&](const ImageContent &) {},
                [&](const AudioContent &) {},
                [&](const ToolUseContent &c) {
                    QJsonObject functionCallItem;
                    functionCallItem["type"] = "function_call";
                    functionCallItem["call_id"] = c.id;
                    functionCallItem["name"] = c.name;
                    functionCallItem["arguments"] = QString::fromUtf8(
                        QJsonDocument(c.input).toJson(QJsonDocument::Compact));
                    items.append(functionCallItem);
                },
                [&](const ToolResultContent &) {},
                [&](const ThinkingContent &c) {
                    if (!includeReasoning || c.itemId.isEmpty()
                        || c.encryptedContent.isEmpty())
                        return;

                    QJsonObject reasoningItem;
                    reasoningItem["type"] = "reasoning";
                    reasoningItem["id"] = c.itemId;
                    reasoningItem["encrypted_content"] = c.encryptedContent;
                    reasoningItem["summary"] = QJsonArray{};
                    items.append(reasoningItem);
                },
                [&](const RedactedThinkingContent &) {}},
            block);
    }

    if (!textContent.isEmpty()) {
        QJsonObject message;
        message["role"] = "assistant";
        message["content"] = textContent;
        items.insert(qBound(0, textPosition, items.size()), message);
    }

    return items;
}

QJsonObject OpenAIResponsesMessage::toResponsesInnerBlock(const ToolContent &block)
{
    return std::visit(
        overloaded{
            [](const TextContent &c) -> QJsonObject {
                return QJsonObject{{"type", "input_text"}, {"text", c.text}};
            },
            [](const ImageContent &c) -> QJsonObject {
                if (c.isUrl()) {
                    return QJsonObject{
                        {"type", "input_image"},
                        {"image_url", c.url().toString()},
                        {"detail", "auto"}};
                }
                const QString mime = c.mimeType.isEmpty() ? QStringLiteral("image/png")
                                                          : c.mimeType;
                return QJsonObject{
                    {"type", "input_image"},
                    {"image_url", QStringLiteral("data:%1;base64,%2").arg(mime, c.base64())},
                    {"detail", "auto"}};
            },
            [](const AudioContent &c) -> QJsonObject {
                return QJsonObject{
                    {"type", "input_text"},
                    {"text",
                     QString("[audio: %1]")
                         .arg(c.mimeType.isEmpty() ? QStringLiteral("unknown") : c.mimeType)}};
            },
            [](const ResourceContent &c) -> QJsonObject {
                if (!c.isBlob() && !c.text().isEmpty())
                    return QJsonObject{{"type", "input_text"}, {"text", c.text()}};
                return QJsonObject{
                    {"type", "input_text"}, {"text", QString("[resource: %1]").arg(c.uri)}};
            },
            [](const ResourceLinkContent &c) -> QJsonObject {
                return QJsonObject{
                    {"type", "input_text"}, {"text", QString("[resource link: %1]").arg(c.uri)}};
            }},
        block);
}


QJsonArray OpenAIResponsesMessage::createToolResultItems(
    const QHash<QString, ToolResult> &toolResults) const
{
    return mapToolResults(
        toolResults, [](const ToolUseContent &use, const ToolResult &r, QJsonArray &out) {
            QJsonObject item;
            item["type"] = "function_call_output";
            item["call_id"] = use.id;

            if (r.hasOnlyText()) {
                item["output"] = toolResultText(r);
            } else {
                QJsonArray blocks;
                for (const ToolContent &block : r.content)
                    blocks.append(toResponsesInnerBlock(block));
                item["output"] = blocks;
            }

            out.append(item);
        });
}

QString OpenAIResponsesMessage::accumulatedText() const
{
    QString text;
    for (const TurnContent &block : m_currentBlocks) {
        if (const auto *textContent = std::get_if<TextContent>(&block))
            text += textContent->text;
    }
    return text;
}

void OpenAIResponsesMessage::updateStateFromStatus()
{
    if (m_status == "completed") {
        if (!getCurrentToolUseContent().isEmpty()) {
            m_state = MessageState::RequiresToolExecution;
        } else {
            m_state = MessageState::Complete;
        }
    } else if (m_status == "in_progress") {
        m_state = MessageState::Building;
    } else if (m_status == "failed" || m_status == "cancelled" || m_status == "incomplete") {
        m_state = MessageState::Final;
    } else {
        m_state = MessageState::Building;
    }
}

int OpenAIResponsesMessage::getOrCreateTextItemIndex()
{
    return getOrCreateTextContentIndex();
}

void OpenAIResponsesMessage::startNewContinuation()
{
    m_toolCalls.clear();
    m_thinkingBlocks.clear();

    BaseMessage::startNewContinuation();

    m_pendingToolArguments.clear();
    m_status.clear();
}

} // namespace LLMQore
