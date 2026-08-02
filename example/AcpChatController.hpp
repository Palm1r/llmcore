// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/AcpAgentConfig.hpp>

#include "ChatSession.hpp"

namespace LLMQore::Acp {
class AcpClient;
}

class AcpChatController : public ChatSession
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("AcpChatController is created by ChatController")

public:
    explicit AcpChatController(QObject *parent = nullptr);
    ~AcpChatController() override;

    bool launch(const QString &agentName, LLMQore::Acp::AcpAgentConfig config);

    void send(const QString &text, const QString &model) override;
    void stop() override;

private:
    void wireAgent();
    void startSession(const QString &cwd);

    LLMQore::Acp::AcpClient *m_client = nullptr;
    QString m_sessionId;
};
