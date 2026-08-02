// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/BaseMessage.hpp>
#include <LLMQore/Conversation.hpp>
#include <LLMQore/ToolDialect.hpp>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore {

class OllamaMessage : public BaseMessage
{
    Q_OBJECT
public:
    explicit OllamaMessage(QObject *parent = nullptr);

    // How this provider spells tool schemas on the way out.
    static const ToolDialect &toolDialect();

    void handleContentDelta(const QString &content);
    void handleToolCall(const QJsonObject &toolCall);
    void handleThinkingDelta(const QString &thinking);
    void handleThinkingComplete(const QString &signature);
    void handleDone(bool done, const QString &doneReason = {});

    QString stopReason() const override { return m_doneReason; }

    // The single turn-to-wire mapping for this provider, shared by the in-flight
    // continuation path (toProviderFormat) and the Conversation replay path
    // (OllamaClient::buildConversationPayload) so the two cannot drift.
    [[nodiscard]] static QJsonObject serializeTurn(
        TurnRole role, const QList<TurnContent> &blocks);

    QJsonObject toProviderFormat() const;
    QJsonArray createToolResultMessages(const QHash<QString, ToolResult> &toolResults) const;

    bool isAccumulatingToolCall() const;

    void startNewContinuation() override;

private:
    bool m_done = false;
    QString m_doneReason;
    QString m_accumulatedContent;
    bool m_contentAddedToTextBlock = false;
    int m_currentThinkingIndex = -1;
    quint64 m_toolCallSequence = 0;

    QString makeToolCallId(const QString &name);
    void updateStateFromDone();
    bool tryParseToolCall();
    QString stripMarkdownCodeFence(const QString &content) const;
    int getOrCreateThinkingContentIndex();
};

} // namespace LLMQore
