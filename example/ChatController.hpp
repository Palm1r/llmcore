// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>

#include <LLMQore/AcpAgentRegistry.hpp>

#include "ChatSession.hpp"

class ChatController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(ChatSession *session READ session NOTIFY sessionChanged)
    Q_PROPERTY(QStringList acpAgentNames READ acpAgentNames CONSTANT)

public:
    explicit ChatController(QObject *parent = nullptr);

    ChatSession *session() const { return m_session; }
    QStringList acpAgentNames() const;

    Q_INVOKABLE void setupProvider(
        const QString &provider, const QString &url, const QString &apiKey);
    Q_INVOKABLE QString envApiKey(const QString &provider) const;

signals:
    void sessionChanged();

private:
    void setSession(ChatSession *session);

    LLMQore::Acp::AcpAgentRegistry m_acpRegistry;
    ChatSession *m_session = nullptr;
    QString m_connectedTo;
};
