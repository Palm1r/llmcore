// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>

#include <LLMQore/ContentBlocks.hpp>
#include <LLMQore/LLMQore_global.h>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore {

enum class TurnRole { User, Assistant, Tool };

struct LLMQORE_EXPORT Turn
{
    TurnRole role = TurnRole::User;
    QList<TurnContent> content;

    [[nodiscard]] QString text() const;
};

// An ordered list of turns, not a validating aggregate. The add* methods append
// blindly: nothing here checks role alternation, nor that a tool result matches a
// ToolUseContent in the preceding assistant turn. Providers reject a malformed
// history with a 400, so the caller owns keeping it well-formed — in particular,
// stage a user turn on a copy and commit it only once the request succeeds, since
// a cancelled or failed request leaves no assistant reply to pair it with.
class LLMQORE_EXPORT Conversation
{
public:
    void setSystem(const QString &system);
    [[nodiscard]] QString system() const;

    void addUser(const QString &text);
    void addUser(QList<TurnContent> content);
    void addAssistant(const QString &text);
    void addAssistant(QList<TurnContent> content);
    void addToolResults(QList<ToolResultContent> results);
    void addTurn(Turn turn);

    [[nodiscard]] const QList<Turn> &turns() const noexcept;
    [[nodiscard]] bool isEmpty() const noexcept;
    void clear();

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static Conversation fromJson(const QJsonObject &obj);

private:
    QString m_system;
    QList<Turn> m_turns;
};

} // namespace LLMQore

Q_DECLARE_METATYPE(LLMQore::Turn)
Q_DECLARE_METATYPE(LLMQore::Conversation)
