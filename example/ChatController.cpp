// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "ChatController.hpp"
#include "AcpChatController.hpp"
#include "LlmChatController.hpp"

#include <optional>

#include <QDir>
#include <QHash>
#include <QStringList>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace Acp = LLMQore::Acp;

namespace {

std::optional<Acp::AcpAgentConfig> acpConfigFor(
    const Acp::AcpAgentRegistry &registry, const QString &provider, const QString &cwd)
{
    for (const Acp::AcpAgentEntry &entry : registry.entries()) {
        if (entry.name == provider)
            return registry.config(entry.id, cwd);
    }
    return std::nullopt;
}

QString interactivePath()
{
    static const QString cached = [] {
        const QString shell = qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
        QProcess process;
        process.start(shell, {QStringLiteral("-lc"), QStringLiteral("printf %s \"$PATH\"")});
        if (process.waitForStarted(2000) && process.waitForFinished(3000)) {
            const QString out
                = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
            if (!out.isEmpty())
                return out;
        }
        return qEnvironmentVariable("PATH");
    }();
    return cached;
}

} // namespace

ChatController::ChatController(QObject *parent)
    : QObject(parent)
{
    const QString path
        = qEnvironmentVariable("LLMQORE_ACP_AGENTS", QStringLiteral(":/agents.json"));
    m_acpRegistry.loadFromFile(path);

    m_session = new LlmChatController(this);
}

QStringList ChatController::acpAgentNames() const
{
    QStringList names;
    for (const Acp::AcpAgentEntry &entry : m_acpRegistry.entries())
        names.append(entry.name);
    return names;
}

QString ChatController::envApiKey(const QString &provider) const
{
    static const QHash<QString, QString> envMap = {
        {"Claude", "CLAUDE_API_KEY"},
        {"OpenAI", "OPENAI_API_KEY"},
        {"OpenAI Responses", "OPENAI_API_KEY"},
        {"DeepSeek", "DEEPSEEK_API_KEY"},
        {"Mistral", "MISTRAL_API_KEY"},
        {"Google AI", "GOOGLE_API_KEY"},
    };

    const QString variable = envMap.value(provider);
    if (variable.isEmpty())
        return {};
    return QProcessEnvironment::systemEnvironment().value(variable);
}

void ChatController::setSession(ChatSession *session)
{
    if (m_session == session)
        return;

    if (m_session)
        m_session->deleteLater();

    m_session = session;
    emit sessionChanged();
}

void ChatController::setupProvider(
    const QString &provider, const QString &url, const QString &apiKey)
{
    const QString fingerprint = QStringList{provider, url, apiKey}.join(QLatin1Char('\n'));
    if (fingerprint == m_connectedTo)
        return;
    m_connectedTo = fingerprint;

    const QString cwd = QDir::currentPath();

    if (auto config = acpConfigFor(m_acpRegistry, provider, cwd)) {
        const QString shellPath = interactivePath();
        const QString resolved = QStandardPaths::findExecutable(
            config->command, shellPath.split(QLatin1Char(':'), Qt::SkipEmptyParts));

        auto *agent = new AcpChatController(this);
        setSession(agent);

        if (resolved.isEmpty())
            return;

        config->command = resolved;
        config->env.append(Acp::EnvVariable{QStringLiteral("PATH"), shellPath});
        agent->launch(provider, *config);
        return;
    }

    auto *llm = new LlmChatController(this);
    setSession(llm);
    llm->connectTo(provider, url, apiKey);

    const int port = qEnvironmentVariableIntValue("LLMQORE_EXPOSE_TOOLS_PORT");
    if (port > 0)
        llm->exposeToolsOverHttp(static_cast<quint16>(port));
}
