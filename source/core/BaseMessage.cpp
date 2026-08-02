// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseMessage.hpp>

#include <QJsonDocument>

namespace LLMQore {

BaseMessage::BaseMessage(QObject *parent)
    : QObject(parent)
{}

BaseMessage::~BaseMessage() = default;

QList<ToolUseContent> BaseMessage::getCurrentToolUseContent() const
{
    QList<ToolUseContent> toolBlocks;
    for (const TurnContent &block : m_currentBlocks) {
        if (const auto *toolContent = std::get_if<ToolUseContent>(&block))
            toolBlocks.append(*toolContent);
    }
    return toolBlocks;
}

QList<ThinkingContent> BaseMessage::getCurrentThinkingContent() const
{
    QList<ThinkingContent> thinkingBlocks;
    for (const TurnContent &block : m_currentBlocks) {
        if (const auto *thinkingContent = std::get_if<ThinkingContent>(&block))
            thinkingBlocks.append(*thinkingContent);
    }
    return thinkingBlocks;
}

QList<PendingThinkingNotification> BaseMessage::takePendingThinkingNotifications()
{
    QList<PendingThinkingNotification> pending;

    for (TurnContent &block : m_currentBlocks) {
        if (auto *thinking = std::get_if<ThinkingContent>(&block)) {
            if (thinking->notified || thinking->thinking.trimmed().isEmpty())
                continue;
            thinking->notified = true;
            pending.append({thinking->thinking, thinking->signature});
        } else if (auto *redacted = std::get_if<RedactedThinkingContent>(&block)) {
            if (redacted->notified)
                continue;
            redacted->notified = true;
            pending.append({QString(), redacted->signature});
        }
    }

    return pending;
}

void BaseMessage::startNewContinuation()
{
    m_currentBlocks.clear();
    m_state = MessageState::Building;
}

int BaseMessage::getOrCreateTextContentIndex()
{
    for (int i = 0; i < m_currentBlocks.size(); ++i) {
        if (std::holds_alternative<TextContent>(m_currentBlocks[i]))
            return i;
    }

    return addCurrentContent(TextContent{});
}

void BaseMessage::appendTextDelta(const QString &delta)
{
    const int index = getOrCreateTextContentIndex();
    if (auto *text = blockAt<TextContent>(index))
        text->text += delta;
}

QJsonArray BaseMessage::mapToolResults(
    const QHash<QString, ToolResult> &toolResults, const ToolResultEmitter &emitFor) const
{
    QJsonArray out;

    for (const ToolUseContent &use : getCurrentToolUseContent()) {
        const auto result = toolResults.constFind(use.id);
        if (result == toolResults.constEnd())
            continue;
        emitFor(use, *result, out);
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
