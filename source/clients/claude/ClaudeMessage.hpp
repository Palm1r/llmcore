// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/BaseMessage.hpp>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore {

class LLMQORE_EXPORT ClaudeMessage : public BaseMessage
{
    Q_OBJECT
public:
    explicit ClaudeMessage(QObject *parent = nullptr);

    void handleContentBlockStart(int index, const QString &blockType, const QJsonObject &data);
    void handleContentBlockDelta(int index, const QString &deltaType, const QJsonObject &delta);
    void handleContentBlockStop(int index);
    void handleStopReason(const QString &stopReason);

    QString stopReason() const override { return m_stopReason; }

    QJsonObject toProviderFormat() const;
    QJsonArray createToolResultsContent(const QHash<QString, ToolResult> &toolResults) const;

    QList<RedactedThinkingContent *> getCurrentRedactedThinkingContent() const;

    void startNewContinuation() override;

private:
    QString m_stopReason;
    QHash<int, QString> m_pendingToolInputs;

    void updateStateFromStopReason();
};

} // namespace LLMQore
