// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseMessage.hpp>

#include <QJsonDocument>

namespace LLMQore {

BaseMessage::BaseMessage(QObject *parent)
    : QObject(parent)
{}

BaseMessage::~BaseMessage()
{
    qDeleteAll(m_currentBlocks);
}

QList<ToolUseContent *> BaseMessage::getCurrentToolUseContent() const
{
    QList<ToolUseContent *> toolBlocks;
    for (auto *block : m_currentBlocks) {
        if (auto *toolContent = dynamic_cast<ToolUseContent *>(block)) {
            toolBlocks.append(toolContent);
        }
    }
    return toolBlocks;
}

QList<ThinkingContent *> BaseMessage::getCurrentThinkingContent() const
{
    QList<ThinkingContent *> thinkingBlocks;
    for (auto *block : m_currentBlocks) {
        if (auto *thinkingContent = dynamic_cast<ThinkingContent *>(block)) {
            thinkingBlocks.append(thinkingContent);
        }
    }
    return thinkingBlocks;
}

void BaseMessage::startNewContinuation()
{
    qDeleteAll(m_currentBlocks);
    m_currentBlocks.clear();
    m_state = MessageState::Building;
}

TextContent *BaseMessage::getOrCreateTextContent()
{
    for (auto *block : m_currentBlocks) {
        if (auto *textContent = dynamic_cast<TextContent *>(block)) {
            return textContent;
        }
    }

    return addCurrentContent<TextContent>();
}

QJsonArray BaseMessage::mapToolResults(
    const QHash<QString, ToolResult> &toolResults, const ToolResultEmitter &emitFor) const
{
    QJsonArray out;

    for (const ToolUseContent *use : getCurrentToolUseContent()) {
        const auto result = toolResults.constFind(use->id());
        if (result == toolResults.constEnd())
            continue;
        emitFor(*use, *result, out);
    }

    return out;
}

QString BaseMessage::toolResultText(const ToolResult &result)
{
    const QString text = result.asText();
    if (result.structuredContent.isEmpty())
        return text;

    const QString json = QString::fromUtf8(
        QJsonDocument(result.structuredContent).toJson(QJsonDocument::Compact));
    return text.isEmpty() ? json : text + QLatin1Char('\n') + json;
}

} // namespace LLMQore
