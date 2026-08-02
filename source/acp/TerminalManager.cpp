// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/TerminalManager.hpp>

#include <QProcess>
#include <QProcessEnvironment>
#include <QPromise>

#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/RpcExceptions.hpp>

namespace LLMQore::Acp {

struct TerminalManager::Terminal
{
    QString id;
    QProcess *process = nullptr;
    QByteArray output;
    qint64 byteLimit = 0;
    bool truncated = false;
    bool finished = false;
    std::optional<int> exitCode;
    QString signal;
    QList<std::shared_ptr<QPromise<WaitForTerminalExitResult>>> waiters;
};

namespace {

WaitForTerminalExitResult exitResult(std::optional<int> exitCode, const QString &signal)
{
    WaitForTerminalExitResult r;
    r.exitCode = exitCode;
    r.signal = signal;
    return r;
}

} // namespace

TerminalManager::TerminalManager(QObject *parent)
    : AcpTerminalProvider(parent)
{}

TerminalManager::~TerminalManager()
{
    for (Terminal *t : m_terminals) {
        if (t->process && t->process->state() != QProcess::NotRunning) {
            t->process->kill();
            t->process->waitForFinished(500);
        }
    }
    qDeleteAll(m_terminals);
}

void TerminalManager::appendOutput(Terminal *t, const QByteArray &data)
{
    if (data.isEmpty())
        return;
    t->output.append(data);
    if (t->byteLimit > 0 && t->output.size() > t->byteLimit) {
        t->output = t->output.right(static_cast<int>(t->byteLimit));
        t->truncated = true;
    }
}

void TerminalManager::onFinished(Terminal *t, int exitCode, int exitStatus)
{
    if (t->process)
        appendOutput(t, t->process->readAllStandardOutput());

    t->finished = true;
    if (exitStatus == static_cast<int>(QProcess::NormalExit))
        t->exitCode = exitCode;
    else
        t->signal = QStringLiteral("killed");

    for (auto &waiter : t->waiters) {
        waiter->addResult(exitResult(t->exitCode, t->signal));
        waiter->finish();
    }
    t->waiters.clear();
}

QFuture<CreateTerminalResult> TerminalManager::createTerminal(const CreateTerminalParams &params)
{
    const QString id = QStringLiteral("term-%1").arg(m_nextId++);

    auto *terminal = new Terminal();
    terminal->id = id;
    terminal->byteLimit = params.outputByteLimit ? *params.outputByteLimit : 0;

    auto *process = new QProcess(this);
    terminal->process = process;
    process->setProcessChannelMode(QProcess::MergedChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const EnvVariable &e : params.env)
        env.insert(e.name, e.value);
    process->setProcessEnvironment(env);

    if (!params.cwd.isEmpty())
        process->setWorkingDirectory(params.cwd);

    Terminal *raw = terminal;
    connect(process, &QProcess::readyReadStandardOutput, this, [this, raw, process]() {
        appendOutput(raw, process->readAllStandardOutput());
    });
    connect(
        process,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this,
        [this, raw](int code, QProcess::ExitStatus status) {
            onFinished(raw, code, static_cast<int>(status));
        });

    process->start(params.command, params.args);

    m_terminals.insert(id, terminal);

    CreateTerminalResult result;
    result.terminalId = id;
    return LLMQore::readyFuture(result);
}

QFuture<TerminalOutputResult> TerminalManager::terminalOutput(
    const QString &, const QString &terminalId)
{
    auto it = m_terminals.find(terminalId);
    if (it == m_terminals.end())
        return LLMQore::failedFuture<TerminalOutputResult>(Rpc::RemoteError(
            Rpc::ErrorCode::InvalidParams,
            QString("Unknown terminalId: %1").arg(terminalId)));

    Terminal *t = it.value();
    TerminalOutputResult r;
    r.output = QString::fromUtf8(t->output);
    r.truncated = t->truncated;
    if (t->finished) {
        ExitStatus es;
        es.exitCode = t->exitCode;
        es.signal = t->signal;
        r.exitStatus = es;
    }
    return LLMQore::readyFuture(r);
}

QFuture<WaitForTerminalExitResult> TerminalManager::waitForExit(
    const QString &, const QString &terminalId)
{
    auto it = m_terminals.find(terminalId);
    if (it == m_terminals.end())
        return LLMQore::failedFuture<WaitForTerminalExitResult>(Rpc::RemoteError(
            Rpc::ErrorCode::InvalidParams,
            QString("Unknown terminalId: %1").arg(terminalId)));

    Terminal *t = it.value();
    if (t->finished)
        return LLMQore::readyFuture(exitResult(t->exitCode, t->signal));

    auto promise = std::make_shared<QPromise<WaitForTerminalExitResult>>();
    promise->start();
    t->waiters.append(promise);
    return promise->future();
}

QFuture<void> TerminalManager::killTerminal(const QString &, const QString &terminalId)
{
    auto it = m_terminals.find(terminalId);
    if (it != m_terminals.end()) {
        Terminal *t = it.value();
        if (t->process && t->process->state() != QProcess::NotRunning)
            t->process->kill();
    }
    return LLMQore::readyFuture();
}

QFuture<void> TerminalManager::releaseTerminal(const QString &, const QString &terminalId)
{
    auto it = m_terminals.find(terminalId);
    if (it == m_terminals.end())
        return LLMQore::readyFuture();

    Terminal *t = it.value();
    m_terminals.erase(it);

    if (t->process) {
        t->process->disconnect(this);
        if (t->process->state() != QProcess::NotRunning) {
            t->process->kill();
            t->process->waitForFinished(500);
        }
    }
    for (auto &waiter : t->waiters) {
        waiter->addResult(exitResult(t->exitCode, t->signal));
        waiter->finish();
    }
    t->waiters.clear();

    delete t->process;
    delete t;
    return LLMQore::readyFuture();
}

} // namespace LLMQore::Acp
