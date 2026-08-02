// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFutureWatcher>
#include <QPointer>

#include <LLMQore/BaseClient.hpp>
#include <LLMQore/Conversation.hpp>

#include "ChatSession.hpp"

namespace LLMQore::Mcp {
class McpServer;
}

class LlmChatController : public ChatSession
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LlmChatController is created by ChatController")

public:
    explicit LlmChatController(QObject *parent = nullptr);
    ~LlmChatController() override;

    bool connectTo(const QString &provider, const QString &url, const QString &apiKey);

    void send(const QString &text, const QString &model) override;
    void stop() override;
    void clear() override;

    void exposeToolsOverHttp(quint16 port);

private:
    LLMQore::BaseClient *createClient(
        const QString &provider, const QString &url, const QString &apiKey);
    void wireClient();
    void registerTools();
    void refreshToolNames();
    void fetchModels();
    void cancelPendingFetch();

    LLMQore::BaseClient *m_client = nullptr;
    LLMQore::Mcp::McpServer *m_toolServer = nullptr;
    QPointer<QFutureWatcher<QList<LLMQore::ModelInfo>>> m_modelWatcher;

    LLMQore::Conversation m_conversation;
    LLMQore::RequestID m_currentRequest;
};
