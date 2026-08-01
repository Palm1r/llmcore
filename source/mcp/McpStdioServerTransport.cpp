// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/McpStdioServerTransport.hpp>

#include <LLMQore/Log.hpp>

#include <LLMQore/RpcLineFramer.hpp>

#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>

#include <cstdio>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace LLMQore::Mcp {

namespace {

class StdioTrace
{
public:
    StdioTrace()
    {
        const QByteArray path = qgetenv("LLMQORE_MCP_TRACE");
        if (path.isEmpty())
            return;
        m_file.setFileName(QString::fromLocal8Bit(path));
        if (!m_file.open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text)) {
            return;
        }
        note(QStringLiteral("--- stdio server transport trace opened ---"));
    }

    bool isEnabled() const { return m_file.isOpen(); }

    void log(const QString &direction, const QByteArray &payload)
    {
        if (!m_file.isOpen())
            return;
        QMutexLocker locker(&m_mutex);
        const QByteArray prefix
            = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ss.zzz").toUtf8() + " ["
              + direction.toUtf8() + "] ";
        m_file.write(prefix);
        m_file.write(payload);
        if (!payload.endsWith('\n'))
            m_file.write("\n", 1);
        m_file.flush();
    }

    void note(const QString &message)
    {
        log(QStringLiteral("NOTE"), message.toUtf8());
    }

private:
    QFile m_file;
    QMutex m_mutex;
};

class StdinReaderThread : public QThread
{
public:
    StdinReaderThread(McpStdioServerTransport *owner, std::shared_ptr<StdioTrace> trace)
        : m_owner(owner)
        , m_trace(std::move(trace))
    {}

    void stopReading()
    {
        m_stop.storeRelease(1);
        QMutexLocker lock(&m_ownerMutex);
        m_owner.clear();
    }

protected:
    void run() override
    {
        m_trace->note(QStringLiteral("reader thread started"));
        Rpc::LineFramer framer;
        char buf[4096];
#ifdef Q_OS_WIN
        const int fd = _fileno(stdin);
#else
        const int fd = fileno(stdin);
#endif
        while (m_stop.loadAcquire() == 0) {
#ifdef Q_OS_WIN
            const int n = _read(fd, buf, static_cast<unsigned>(sizeof(buf)));
#else
            const ssize_t n = ::read(fd, buf, sizeof(buf));
#endif
            if (n == 0) {
                m_trace->note(QStringLiteral("stdin EOF"));
                break;
            }
            if (n < 0) {
                m_trace->note(QStringLiteral("stdin read error"));
                break;
            }
            m_trace->log(
                QStringLiteral("RAW<"), QByteArray(buf, static_cast<int>(n)));
            const QByteArrayList frames = framer.append(QByteArray(buf, static_cast<int>(n)));
            for (const QByteArray &frame : frames) {
                m_trace->log(QStringLiteral("IN <"), frame);
                QJsonParseError err{};
                const QJsonDocument doc = QJsonDocument::fromJson(frame, &err);
                if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                    m_trace->note(
                        QString("PARSE FAIL: %1").arg(err.errorString()));
                    qCWarning(llmMcpLog).noquote()
                        << QString("Dropping invalid stdin JSON line: %1")
                               .arg(QString::fromUtf8(frame));
                    continue;
                }
                QJsonObject obj = doc.object();
                postToOwner("messageReceived", Q_ARG(QJsonObject, obj));
                if (m_stop.loadAcquire() != 0)
                    break;
            }
        }
        m_trace->note(QStringLiteral("reader thread exiting"));
        postToOwner("closed");
    }

private:
    template<typename... Args>
    void postToOwner(const char *method, Args &&...args)
    {
        QMutexLocker lock(&m_ownerMutex);
        if (!m_owner)
            return;
        QMetaObject::invokeMethod(
            m_owner.data(), method, Qt::QueuedConnection, std::forward<Args>(args)...);
    }

    QPointer<McpStdioServerTransport> m_owner;
    QMutex m_ownerMutex;
    std::shared_ptr<StdioTrace> m_trace;
    QAtomicInt m_stop{0};
};

} // namespace

struct McpStdioServerTransport::Impl
{
    bool open = false;
    QMutex writeMutex;
    StdinReaderThread *reader = nullptr;
    QIODevice *in = nullptr;
    QIODevice *out = nullptr;
    Rpc::LineFramer framer;
    std::shared_ptr<StdioTrace> trace;
};

McpStdioServerTransport::McpStdioServerTransport(QObject *parent)
    : McpStdioServerTransport(nullptr, nullptr, parent)
{}

McpStdioServerTransport::McpStdioServerTransport(
    QIODevice *input, QIODevice *output, QObject *parent)
    : Rpc::Transport(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->in = input;
    m_impl->out = output;
    m_impl->trace = std::make_shared<StdioTrace>();
}

McpStdioServerTransport::~McpStdioServerTransport()
{
    stop();
}

void McpStdioServerTransport::start()
{
    if (m_impl->open)
        return;

    if (m_impl->in) {
        m_impl->trace->note(QStringLiteral("McpStdioServerTransport::start (injected devices)"));
        m_impl->open = true;
        setState(State::Connected);
        connect(m_impl->in, &QIODevice::readyRead, this, [this]() { readFromDevice(); });
        connect(m_impl->in, &QIODevice::readChannelFinished, this, [this]() {
            readFromDevice();
            m_impl->trace->note(QStringLiteral("input channel finished"));
            emit closed();
        });
        readFromDevice();
        return;
    }

#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stdin, nullptr, _IONBF, 0);

    m_impl->trace->note(QStringLiteral("McpStdioServerTransport::start"));

    m_impl->open = true;
    setState(State::Connected);

    m_impl->reader = new StdinReaderThread(this, m_impl->trace);
    m_impl->reader->start();
}

void McpStdioServerTransport::stop()
{
    if (!m_impl->open)
        return;
    m_impl->open = false;
    if (m_impl->in)
        disconnect(m_impl->in, nullptr, this, nullptr);
    if (m_impl->reader) {
        m_impl->reader->stopReading();
        if (m_impl->reader->isRunning()) {
            m_impl->reader->requestInterruption();
            m_impl->reader->wait(200);
        }
        if (!m_impl->reader->isRunning()) {
            m_impl->reader->deleteLater();
        }
        m_impl->reader = nullptr;
    }
    setState(State::Disconnected);
    emit closed();
}

bool McpStdioServerTransport::isOpen() const
{
    return m_impl->open;
}

void McpStdioServerTransport::send(const QJsonObject &message)
{
    if (!m_impl->open)
        return;
    QMutexLocker locker(&m_impl->writeMutex);
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    m_impl->trace->log(QStringLiteral("OUT>"), payload);
    if (m_impl->out) {
        m_impl->out->write(payload);
        m_impl->out->write("\n", 1);
        return;
    }
    std::fwrite(payload.constData(), 1, static_cast<size_t>(payload.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void McpStdioServerTransport::readFromDevice()
{
    if (!m_impl->open || !m_impl->in)
        return;
    const QByteArray data = m_impl->in->readAll();
    if (data.isEmpty())
        return;
    m_impl->trace->log(QStringLiteral("RAW<"), data);
    const QByteArrayList frames = m_impl->framer.append(data);
    for (const QByteArray &frame : frames) {
        m_impl->trace->log(QStringLiteral("IN <"), frame);
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(frame, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            m_impl->trace->note(QString("PARSE FAIL: %1").arg(err.errorString()));
            qCWarning(llmMcpLog).noquote()
                << QString("Dropping invalid stdin JSON line: %1")
                       .arg(QString::fromUtf8(frame));
            continue;
        }
        emit messageReceived(doc.object());
    }
}

} // namespace LLMQore::Mcp
