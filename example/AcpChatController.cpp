// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "AcpChatController.hpp"

#include <QDir>

#include <LLMQore/AcpClient.hpp>
#include <LLMQore/CallbackPermissionProvider.hpp>
#include <LLMQore/DefaultFileSystemProvider.hpp>
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/TerminalManager.hpp>

namespace Acp = LLMQore::Acp;

AcpChatController::AcpChatController(QObject *parent)
    : ChatSession(parent)
{}

AcpChatController::~AcpChatController()
{
    if (m_client)
        m_client->shutdown();
}

bool AcpChatController::launch(const QString &agentName, Acp::AcpAgentConfig config)
{
    if (m_client) {
        m_client->shutdown();
        m_client->deleteLater();
        m_client = nullptr;
        setBusy(false);
    }
    m_sessionId.clear();

    m_client = new Acp::AcpClient(config.createTransport(this), {}, this);

    m_client->setFileSystemProvider(new Acp::DefaultFileSystemProvider(this));
    m_client->setTerminalProvider(new Acp::TerminalManager(this));
    m_client->setPermissionProvider(new Acp::CallbackPermissionProvider(
        [this](const QString &,
               const Acp::ToolCall &toolCall,
               const QList<Acp::PermissionOption> &options) {
            m_messages.append(
                "tool", QString("[permission] auto-allowed: %1").arg(toolCall.title));
            return options.isEmpty()
                ? Acp::RequestPermissionResult::cancelled()
                : Acp::RequestPermissionResult::selected(options.first().optionId);
        },
        this));

    wireAgent();

    setModelList({agentName});
    setLoadingModels(false);
    setToolNames({});
    setBusy(true);
    setStatus(QString("Starting %1 ...").arg(config.command));

    startSession(QDir::currentPath());
    return true;
}

void AcpChatController::wireAgent()
{
    connect(m_client, &Acp::AcpClient::agentMessageChunk, this,
            [this](const QString &, const Acp::ContentBlock &content) {
                m_messages.appendOrCreate("assistant", content.text);
            });

    connect(m_client, &Acp::AcpClient::agentThoughtChunk, this,
            [this](const QString &, const Acp::ContentBlock &content) {
                setStatus(QStringLiteral("Thinking: %1").arg(content.text.left(60)));
            });

    connect(m_client, &Acp::AcpClient::toolCallStarted, this,
            [this](const QString &, const Acp::ToolCall &toolCall) {
                const QString kind = toolCall.kind.isEmpty() ? QStringLiteral("tool")
                                                             : toolCall.kind;
                m_messages.append("tool", QString("[%1] %2").arg(kind, toolCall.title));
            });

    connect(m_client, &Acp::AcpClient::toolCallUpdated, this,
            [this](const QString &, const Acp::ToolCall &toolCall) {
                if (!toolCall.status.isEmpty())
                    setStatus(QString("Tool %1: %2").arg(toolCall.title, toolCall.status));
            });

    connect(m_client, &Acp::AcpClient::promptFinished, this,
            [this](const QString &, const QString &stopReason) {
                setBusy(false);
                setStatus(QString("Ready (%1)").arg(stopReason));
            });

    connect(m_client, &Acp::AcpClient::errorOccurred, this, [this](const QString &error) {
        m_messages.append("error", error);
        setBusy(false);
        setStatus("Error");
    });
}

void AcpChatController::startSession(const QString &cwd)
{
    auto *client = m_client;

    LLMQore::compat(client->connectAndInitialize())
        .then(client,
              [client, cwd](const Acp::InitializeResult &) -> QFuture<Acp::NewSessionResult> {
                  Acp::NewSessionParams params;
                  params.cwd = cwd;
                  return client->newSession(params);
              })
        .unwrap()
        .then(this,
              [this, client](const Acp::NewSessionResult &session) -> int {
                  if (m_client != client)
                      return 0;
                  m_sessionId = session.sessionId;
                  setBusy(false);
                  setStatus("ACP session ready");
                  return 0;
              })
        .onFailed(this, [this, client](const std::exception &e) -> int {
            if (m_client != client)
                return 0;
            m_messages.append("error", QString::fromUtf8(e.what()));
            setBusy(false);
            setStatus("Failed to start agent");
            return 0;
        });
}

void AcpChatController::send(const QString &text, const QString &)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || busy())
        return;

    if (!m_client || m_sessionId.isEmpty()) {
        setStatus("Agent not ready yet");
        return;
    }

    m_messages.append("user", trimmed);
    setBusy(true);
    setStatus("Waiting for agent...");

    LLMQore::compat(m_client->prompt(m_sessionId, {Acp::ContentBlock::makeText(trimmed)}))
        .onFailed(this, [this](const std::exception &e) -> Acp::PromptResult {
            m_messages.append("error", QString::fromUtf8(e.what()));
            setBusy(false);
            setStatus("Request failed");
            return Acp::PromptResult{};
        });
}

void AcpChatController::stop()
{
    if (!busy() || !m_client || m_sessionId.isEmpty())
        return;

    m_client->cancel(m_sessionId);
}
