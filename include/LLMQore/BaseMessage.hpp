// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

#include <QHash>
#include <QJsonArray>

#include <LLMQore/LLMQore_global.h>

#include <LLMQore/ContentBlocks.hpp>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore {

class LLMQORE_EXPORT BaseMessage : public QObject
{
    Q_OBJECT
public:
    explicit BaseMessage(QObject *parent = nullptr);
    ~BaseMessage() override;

    MessageState state() const { return m_state; }
    const QList<ContentBlock *> &getCurrentBlocks() const { return m_currentBlocks; }

    virtual QString stopReason() const { return {}; }

    QList<ToolUseContent *> getCurrentToolUseContent() const;
    QList<ThinkingContent *> getCurrentThinkingContent() const;

    virtual void startNewContinuation();

protected:
    using ToolResultEmitter
        = std::function<void(const ToolUseContent &, const ToolResult &, QJsonArray &)>;
    [[nodiscard]] QJsonArray mapToolResults(
        const QHash<QString, ToolResult> &toolResults, const ToolResultEmitter &emitFor) const;

    [[nodiscard]] static QString toolResultText(const ToolResult &result);

    MessageState m_state = MessageState::Building;
    QList<ContentBlock *> m_currentBlocks;

    TextContent *getOrCreateTextContent();

    template<typename T, typename... Args>
    T *addCurrentContent(Args &&...args)
    {
        return addContentBlock<T>(m_currentBlocks, std::forward<Args>(args)...);
    }
};

} // namespace LLMQore
