// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/BaseMessage.hpp>
#include <LLMQore/OpenAIResponsesClient.hpp>
#include <LLMQore/ToolDialect.hpp>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore {

class OpenAIResponsesMessage : public BaseMessage
{
    Q_OBJECT
public:
    explicit OpenAIResponsesMessage(QObject *parent = nullptr);

    // How this provider spells tool schemas on the way out.
    static const ToolDialect &toolDialect();

    void handleContentDelta(const QString &text);
    void handleToolCallStart(const QString &callId, const QString &name);
    void handleToolCallDelta(const QString &callId, const QString &argumentsDelta);
    void handleToolCallComplete(const QString &callId, const QString &finalArguments = QString());
    void handleReasoningStart(const QString &itemId);
    void handleReasoningDelta(const QString &itemId, const QString &text);
    void handleReasoningComplete(const QString &itemId);
    void handleReasoningEncryptedContent(const QString &itemId, const QString &encryptedContent);
    void handleStatus(const QString &status);

    QString stopReason() const override { return m_status; }

    // The single turn-to-wire mapping for this provider, shared by the in-flight
    // continuation path (toItemsFormat) and the Conversation replay path
    // (OpenAIResponsesClient::buildConversationPayload) so the two cannot drift.
    [[nodiscard]] static QList<QJsonObject> serializeTurn(
        TurnRole role, const QList<TurnContent> &blocks, ReasoningPersistence reasoning);

    QList<QJsonObject> toItemsFormat(ReasoningPersistence reasoning) const;
    QJsonArray createToolResultItems(const QHash<QString, ToolResult> &toolResults) const;
    static QJsonObject toResponsesInnerBlock(const ToolContent &block);

    QString accumulatedText() const;

    bool hasToolCalls() const noexcept { return !m_toolCalls.isEmpty(); }
    bool hasThinkingContent() const noexcept { return !m_thinkingBlocks.isEmpty(); }

    void startNewContinuation() override;

private:
    QString m_status;
    QHash<QString, QString> m_pendingToolArguments;
    QHash<QString, int> m_toolCalls;
    QHash<QString, int> m_thinkingBlocks;

    void updateStateFromStatus();
    int getOrCreateTextItemIndex();
};

} // namespace LLMQore
