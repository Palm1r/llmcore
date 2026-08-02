// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseMessage.hpp>

namespace LLMQore {

BaseMessage::BaseMessage(QObject *parent)
    : QObject(parent)
{}

BaseMessage::~BaseMessage() = default;

QList<ToolUseContent> BaseMessage::currentToolUseContent() const
{
    QList<ToolUseContent> toolBlocks;
    for (const TurnContent &block : m_currentBlocks) {
        if (const auto *toolContent = std::get_if<ToolUseContent>(&block))
            toolBlocks.append(*toolContent);
    }
    return toolBlocks;
}

QList<ThinkingContent> BaseMessage::currentThinkingContent() const
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

    for (int i = 0; i < m_currentBlocks.size(); ++i) {
        if (m_notifiedThinking.contains(i))
            continue;

        const TurnContent &block = m_currentBlocks.at(i);
        if (const auto *thinking = std::get_if<ThinkingContent>(&block)) {
            if (thinking->thinking.trimmed().isEmpty())
                continue;
            m_notifiedThinking.insert(i);
            pending.append({thinking->thinking, thinking->signature});
        } else if (const auto *redacted = std::get_if<RedactedThinkingContent>(&block)) {
            m_notifiedThinking.insert(i);
            pending.append({QString(), redacted->signature});
        }
    }

    return pending;
}

void BaseMessage::removeBlocksIf(const std::function<bool(const TurnContent &)> &predicate)
{
    QSet<int> remapped;
    int kept = 0;

    for (int i = 0; i < m_currentBlocks.size(); ++i) {
        if (predicate(m_currentBlocks.at(i)))
            continue;
        if (m_notifiedThinking.contains(i))
            remapped.insert(kept);
        if (kept != i)
            m_currentBlocks[kept] = std::move(m_currentBlocks[i]);
        ++kept;
    }

    m_currentBlocks.erase(m_currentBlocks.begin() + kept, m_currentBlocks.end());
    m_notifiedThinking = std::move(remapped);
}

void BaseMessage::clearBlocks()
{
    m_currentBlocks.clear();
    m_notifiedThinking.clear();
}

void BaseMessage::startNewContinuation()
{
    clearBlocks();
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

    for (const ToolUseContent &use : currentToolUseContent()) {
        const auto result = toolResults.constFind(use.id);
        if (result == toolResults.constEnd())
            continue;
        emitFor(use, *result, out);
    }

    return out;
}

} // namespace LLMQore
