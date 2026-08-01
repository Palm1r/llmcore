// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <LLMQore/BaseTool.hpp>
#include <LLMQore/McpClient.hpp>
#include <LLMQore/McpServer.hpp>
#include <LLMQore/McpToolBinder.hpp>
#include <LLMQore/RpcPipeTransport.hpp>
#include <LLMQore/ToolRegistry.hpp>

using namespace LLMQore;
using namespace LLMQore::Mcp;

namespace {

template<typename T>
T waitForFuture(const QFuture<T> &future, int timeoutMs = 5000)
{
    if (future.isFinished())
        return future.result();
    QEventLoop loop;
    QFutureWatcher<T> watcher;
    QObject::connect(&watcher, &QFutureWatcher<T>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return future.result();
}

void pumpEventLoop(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

bool waitForSignal(QSignalSpy &spy, int count, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (spy.count() < count && timer.elapsed() < timeoutMs)
        pumpEventLoop(20);
    return spy.count() >= count;
}

class CannedTool : public BaseTool
{
    Q_OBJECT
public:
    CannedTool(QString id, QString reply, QObject *parent = nullptr)
        : BaseTool(parent)
        , m_id(std::move(id))
        , m_reply(std::move(reply))
    {}
    QString id() const override { return m_id; }
    QString displayName() const override { return m_id; }
    QString description() const override { return QString("Replies with %1").arg(m_reply); }
    QJsonObject parametersSchema() const override
    {
        return QJsonObject{{"type", "object"}};
    }
    QFuture<LLMQore::ToolResult> executeAsync(const QJsonObject &) override
    {
        const QString reply = m_reply;
        return QtConcurrent::run(
            [reply]() -> LLMQore::ToolResult { return LLMQore::ToolResult::text(reply); });
    }

private:
    QString m_id;
    QString m_reply;
};

struct Loopback
{
    Rpc::PipeTransport *serverTransport = nullptr;
    Rpc::PipeTransport *clientTransport = nullptr;
    McpServer *server = nullptr;
    McpClient *client = nullptr;

    explicit Loopback(const QString &serverName)
    {
        auto [st, ct] = Rpc::PipeTransport::createPair();
        serverTransport = st;
        clientTransport = ct;
        server = new McpServer(serverTransport, McpServerConfig{{serverName, "0.0.1"}});
        client = new McpClient(clientTransport);
        server->start();
    }

    ~Loopback()
    {
        delete client;
        delete server;
        delete serverTransport;
        delete clientTransport;
    }

    void initialize() { waitForFuture(client->connectAndInitialize()); }
};

class McpToolBinderTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "tst_McpToolBinder";
            static char *argv[] = {arg0};
            m_app = new QCoreApplication(argc, argv);
        }
    }
    void TearDown() override
    {
        delete m_app;
        m_app = nullptr;
    }
    QCoreApplication *m_app = nullptr;
};

} // namespace

TEST_F(McpToolBinderTest, PrefixesToolIdsWithServerName)
{
    Loopback loop("upstream");
    loop.server->addTool(new CannedTool("echo", "hi", loop.server));
    loop.initialize();

    ToolRegistry registry;
    McpToolBinder binder(&registry);
    QSignalSpy synced(&binder, &McpToolBinder::toolsSynced);

    binder.addClient(loop.client, "alpha");
    ASSERT_TRUE(waitForSignal(synced, 1));

    ASSERT_EQ(registry.registeredTools().size(), 1);
    BaseTool *tool = registry.tool("alpha_echo");
    ASSERT_NE(tool, nullptr);
    EXPECT_EQ(tool->id(), "alpha_echo");
    EXPECT_EQ(registry.tool("echo"), nullptr);
}

TEST_F(McpToolBinderTest, TwoUpstreamsWithSameToolNameDoNotCollide)
{
    Loopback a("upstream-a");
    Loopback b("upstream-b");
    a.server->addTool(new CannedTool("read_file", "from-a", a.server));
    b.server->addTool(new CannedTool("read_file", "from-b", b.server));
    a.initialize();
    b.initialize();

    ToolRegistry registry;
    McpToolBinder binder(&registry);
    QSignalSpy synced(&binder, &McpToolBinder::toolsSynced);

    binder.addClient(a.client, "alpha");
    binder.addClient(b.client, "beta");
    ASSERT_TRUE(waitForSignal(synced, 2));

    ASSERT_EQ(registry.registeredTools().size(), 2);
    BaseTool *toolA = registry.tool("alpha_read_file");
    BaseTool *toolB = registry.tool("beta_read_file");
    ASSERT_NE(toolA, nullptr);
    ASSERT_NE(toolB, nullptr);

    EXPECT_EQ(waitForFuture(toolA->executeAsync(QJsonObject{})).asText(), "from-a");
    EXPECT_EQ(waitForFuture(toolB->executeAsync(QJsonObject{})).asText(), "from-b");
}

TEST_F(McpToolBinderTest, DiffResyncFollowsToolsChanged)
{
    Loopback loop("upstream");
    loop.server->addTool(new CannedTool("echo", "hi", loop.server));
    loop.initialize();

    ToolRegistry registry;
    McpToolBinder binder(&registry);
    QSignalSpy synced(&binder, &McpToolBinder::toolsSynced);

    binder.addClient(loop.client, "srv");
    ASSERT_TRUE(waitForSignal(synced, 1));
    EXPECT_EQ(registry.registeredTools().size(), 1);

    loop.server->addTool(new CannedTool("upper", "HI", loop.server));
    ASSERT_TRUE(waitForSignal(synced, 2));
    EXPECT_NE(registry.tool("srv_echo"), nullptr);
    EXPECT_NE(registry.tool("srv_upper"), nullptr);

    loop.server->removeTool("upper");
    ASSERT_TRUE(waitForSignal(synced, 3));
    EXPECT_NE(registry.tool("srv_echo"), nullptr);
    EXPECT_EQ(registry.tool("srv_upper"), nullptr);
    EXPECT_EQ(registry.registeredTools().size(), 1);
}

TEST_F(McpToolBinderTest, RemovesToolsWhenClientDestroyed)
{
    Loopback loop("upstream");
    loop.server->addTool(new CannedTool("echo", "hi", loop.server));
    loop.initialize();

    ToolRegistry registry;
    McpToolBinder binder(&registry);
    QSignalSpy synced(&binder, &McpToolBinder::toolsSynced);

    binder.addClient(loop.client, "srv");
    ASSERT_TRUE(waitForSignal(synced, 1));
    EXPECT_EQ(registry.registeredTools().size(), 1);

    delete loop.client;
    loop.client = nullptr;
    pumpEventLoop(50);

    EXPECT_EQ(registry.registeredTools().size(), 0);
}

TEST_F(McpToolBinderTest, RemoveClientDetachesWithoutTouchingClient)
{
    Loopback loop("upstream");
    loop.server->addTool(new CannedTool("echo", "hi", loop.server));
    loop.initialize();

    ToolRegistry registry;
    McpToolBinder binder(&registry);
    QSignalSpy synced(&binder, &McpToolBinder::toolsSynced);

    binder.addClient(loop.client, "srv");
    ASSERT_TRUE(waitForSignal(synced, 1));

    binder.removeClient(loop.client);
    pumpEventLoop(50);
    EXPECT_EQ(registry.registeredTools().size(), 0);
    EXPECT_TRUE(loop.client->isInitialized());
}

TEST_F(McpToolBinderTest, ReconnectsAfterTransportLoss)
{
    Loopback loop("upstream");
    loop.server->addTool(new CannedTool("echo", "hi", loop.server));
    loop.initialize();

    ToolRegistry registry;
    McpToolBinder binder(&registry);
    QSignalSpy synced(&binder, &McpToolBinder::toolsSynced);
    QSignalSpy dropped(&binder, &McpToolBinder::serverDisconnected);

    binder.addClient(loop.client, "srv", /*autoReconnect*/ true);
    ASSERT_TRUE(waitForSignal(synced, 1));
    EXPECT_EQ(registry.registeredTools().size(), 1);

    loop.clientTransport->stop();
    ASSERT_TRUE(waitForSignal(dropped, 1));
    EXPECT_EQ(registry.registeredTools().size(), 0);

    ASSERT_TRUE(waitForSignal(synced, 2, 10000));
    EXPECT_NE(registry.tool("srv_echo"), nullptr);
    EXPECT_EQ(waitForFuture(registry.tool("srv_echo")->executeAsync(QJsonObject{})).asText(), "hi");
}

#include "tst_McpToolBinder.moc"
