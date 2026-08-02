// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/RpcStdioTransport.hpp>

#include <LLMQore/Log.hpp>

#include <LLMQore/RpcLineFramer.hpp>
#include <LLMQore/RpcNdJsonCodec.hpp>

#include <QFileInfo>

namespace LLMQore::Rpc {

struct StdioClientTransport::Impl
{
    NdJsonCodec stdoutCodec;
    LineFramer stderrFramer;
};

StdioClientTransport::StdioClientTransport(Rpc::StdioLaunchConfig config, QObject *parent)
    : Transport(parent)
    , m_config(std::move(config))
    , m_impl(std::make_unique<Impl>())
{}

StdioClientTransport::~StdioClientTransport()
{
    stop();
}

void StdioClientTransport::start()
{
    if (m_process)
        return;

    m_process = new QProcess(this);
    m_process->setProcessEnvironment(m_config.environment);
    if (!m_config.workingDirectory.isEmpty())
        m_process->setWorkingDirectory(m_config.workingDirectory);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &StdioClientTransport::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &StdioClientTransport::onReadyReadStderr);
    connect(m_process, &QProcess::errorOccurred, this, &StdioClientTransport::onProcessErrorOccurred);
    connect(
        m_process,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this,
        &StdioClientTransport::onProcessFinished);

    QString program = m_config.program;
    QStringList arguments = m_config.arguments;

#ifdef Q_OS_WIN
    const QString lower = program.toLower();
    const bool isBatchExt = lower.endsWith(".cmd") || lower.endsWith(".bat");
    const bool isKnownWrapper
        = (lower == "npx" || lower == "npm" || lower == "pnpm" || lower == "yarn"
           || lower == "uvx");
    if (isBatchExt || isKnownWrapper) {
        QStringList wrapped;
        wrapped << QStringLiteral("/c") << program;
        wrapped.append(arguments);
        program = QStringLiteral("cmd.exe");
        arguments = wrapped;
    }
#endif

    qCInfo(llmRpcLog).noquote()
        << QString("Starting child process: %1 %2").arg(program, arguments.join(' '));
    m_process->start(program, arguments);

    if (!m_process->waitForStarted(m_config.startupTimeoutMs)) {
        const QString reason = QString("Failed to start '%1': %2").arg(program, m_process->errorString());
        qCWarning(llmRpcLog).noquote() << reason;
        emit errorOccurred(reason);
        emit closed();
        return;
    }
}

void StdioClientTransport::stop()
{
    if (!m_process)
        return;

    m_process->disconnect(this);
    if (m_process->state() != QProcess::NotRunning) {
        m_process->closeWriteChannel();
        if (!m_process->waitForFinished(m_config.gracefulStopTimeoutMs)) {
            m_process->kill();
            m_process->waitForFinished(m_config.killTimeoutMs);
        }
    }
    m_process->deleteLater();
    m_process = nullptr;
    m_impl->stdoutCodec.clear();
    m_impl->stderrFramer.clear();

    emit closed();
}

bool StdioClientTransport::isOpen() const
{
    return m_process && m_process->state() == QProcess::Running;
}

void StdioClientTransport::send(const QJsonObject &message)
{
    if (!isOpen()) {
        emit errorOccurred(QStringLiteral("Transport not open"));
        return;
    }
    m_process->write(NdJsonCodec::encode(message));
}

void StdioClientTransport::onReadyReadStdout()
{
    if (!m_process)
        return;
    const QList<QJsonObject> messages
        = m_impl->stdoutCodec.decode(m_process->readAllStandardOutput());
    for (const QJsonObject &message : messages)
        emit messageReceived(message);
}

void StdioClientTransport::onReadyReadStderr()
{
    if (!m_process)
        return;
    const QByteArrayList lines = m_impl->stderrFramer.append(m_process->readAllStandardError());
    for (const QByteArray &line : lines) {
        const QString text = QString::fromUtf8(line);
        qCInfo(llmRpcLog).noquote() << "stderr:" << text;
        emit stderrLine(text);
    }
}

void StdioClientTransport::onProcessErrorOccurred(QProcess::ProcessError error)
{
    const QString reason = QString("QProcess error %1: %2")
                               .arg(static_cast<int>(error))
                               .arg(m_process ? m_process->errorString() : QString());
    qCWarning(llmRpcLog).noquote() << reason;
    emit errorOccurred(reason);
}

void StdioClientTransport::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    qCInfo(llmRpcLog).noquote() << QString("child process exited code=%1 status=%2")
                                       .arg(exitCode)
                                       .arg(static_cast<int>(status));
    emit closed();
}

} // namespace LLMQore::Rpc
