// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonValue>

#include <LLMQore/BaseMessage.hpp>
#include <LLMQore/ToolDialect.hpp>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore {

class OpenAIMessage : public BaseMessage
{
    Q_OBJECT
public:
    struct ContentParts
    {
        QString thinking;
        QString text;
    };

    // Splits an OpenAI-compatible "content" value into reasoning and answer text.
    // A plain string is answer text; Mistral Magistral sends an array of typed chunks.
    [[nodiscard]] static ContentParts splitContentParts(const QJsonValue &content);

    explicit OpenAIMessage(QObject *parent = nullptr);

    // How this provider spells tool schemas on the way out.
    static const ToolDialect &toolDialect();

    void handleContentDelta(const QString &content);
    void handleReasoningDelta(const QString &reasoning);
    void handleToolCallStart(int index, const QString &id, const QString &name);
    void handleToolCallDelta(int index, const QString &argumentsDelta);
    void handleToolCallComplete(int index);
    void completeAllPendingToolCalls();
    void handleFinishReason(const QString &finishReason);

    QString stopReason() const override { return m_finishReason; }

    QJsonObject toProviderFormat() const;
    QJsonArray createToolResultMessages(const QHash<QString, ToolResult> &toolResults) const;

    void startNewContinuation() override;

private:
    QString m_finishReason;
    QHash<int, QString> m_pendingToolArguments;
    QHash<int, ToolUseContent *> m_toolCallByIndex;
    ThinkingContent *m_currentThinkingContent = nullptr;

    void updateStateFromFinishReason();
    ThinkingContent *getOrCreateThinkingContent();
};

} // namespace LLMQore
