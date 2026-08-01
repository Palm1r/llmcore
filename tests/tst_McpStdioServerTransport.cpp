// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTimer>

#include <LLMQore/McpStdioServerTransport.hpp>
#include <LLMQore/RpcStdioTransport.hpp>

#include "TestHelpers.hpp"

using namespace LLMQore;
using namespace LLMQore::Mcp;

namespace {

// Two QBuffer views over one byte array: the transport reads from `reader`
// while the test appends through `writer` and pokes readyRead, emulating an
// incrementally arriving stdin.
struct DevicePair
{
    QByteArray inBytes;
    QBuffer reader;
    QBuffer writer;
    QByteArray outBytes;
    QBuffer out;

    DevicePair()
    {
        reader.setBuffer(&inBytes);
        writer.setBuffer(&inBytes);
        out.setBuffer(&outBytes);
        const bool opened = reader.open(QIODevice::ReadOnly)
            && writer.open(QIODevice::WriteOnly | QIODevice::Append)
            && out.open(QIODevice::WriteOnly);
        if (!opened)
            ADD_FAILURE() << "failed to open QBuffer devices";
    }

    void feed(const QByteArray &chunk)
    {
        writer.write(chunk);
        QMetaObject::invokeMethod(&reader, "readyRead", Qt::DirectConnection);
    }
};

class McpStdioServerTransportTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "tst_McpStdioServerTransport";
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

TEST_F(McpStdioServerTransportTest, FramesPartialChunksAcrossFeeds)
{
    DevicePair pair;
    McpStdioServerTransport transport(&pair.reader, &pair.out);
    QSignalSpy received(&transport, &Rpc::Transport::messageReceived);

    transport.start();
    ASSERT_TRUE(transport.isOpen());

    pair.feed(R"({"jsonrpc":"2.0","met)");
    EXPECT_EQ(received.size(), 0);

    pair.feed("hod\":\"ping\"}\n");
    ASSERT_EQ(received.size(), 1);
    const QJsonObject msg = received.takeFirst().at(0).toJsonObject();
    EXPECT_EQ(msg.value("method").toString(), "ping");
}

TEST_F(McpStdioServerTransportTest, HandlesCrlfAndEmptyLines)
{
    DevicePair pair;
    McpStdioServerTransport transport(&pair.reader, &pair.out);
    QSignalSpy received(&transport, &Rpc::Transport::messageReceived);

    transport.start();
    pair.feed("\r\n\n{\"id\":\"1\"}\r\n{\"id\":\"2\"}\n\r\n");

    ASSERT_EQ(received.size(), 2);
    EXPECT_EQ(received.at(0).at(0).toJsonObject().value("id").toString(), "1");
    EXPECT_EQ(received.at(1).at(0).toJsonObject().value("id").toString(), "2");
}

TEST_F(McpStdioServerTransportTest, DropsInvalidJsonLinesAndRecovers)
{
    DevicePair pair;
    McpStdioServerTransport transport(&pair.reader, &pair.out);
    QSignalSpy received(&transport, &Rpc::Transport::messageReceived);

    transport.start();
    pair.feed("this is not json\n{\"id\":\"ok\"}\n[1,2,3]\n");

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received.at(0).at(0).toJsonObject().value("id").toString(), "ok");
}

TEST_F(McpStdioServerTransportTest, DrainsInputAlreadyBufferedBeforeStart)
{
    DevicePair pair;
    pair.writer.write("{\"id\":\"early\"}\n");

    McpStdioServerTransport transport(&pair.reader, &pair.out);
    QSignalSpy received(&transport, &Rpc::Transport::messageReceived);

    transport.start();
    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received.at(0).at(0).toJsonObject().value("id").toString(), "early");
}

TEST_F(McpStdioServerTransportTest, WritesResponsesInOrderNewlineFramed)
{
    DevicePair pair;
    McpStdioServerTransport transport(&pair.reader, &pair.out);
    transport.start();

    const QJsonObject first{{"id", "1"}, {"result", "a"}};
    const QJsonObject second{{"id", "2"}, {"result", "b"}};
    transport.send(first);
    transport.send(second);

    const QByteArray expected = QJsonDocument(first).toJson(QJsonDocument::Compact) + '\n'
                                + QJsonDocument(second).toJson(QJsonDocument::Compact) + '\n';
    EXPECT_EQ(pair.outBytes, expected);
}

TEST_F(McpStdioServerTransportTest, LifecycleOpenClose)
{
    DevicePair pair;
    McpStdioServerTransport transport(&pair.reader, &pair.out);
    QSignalSpy closedSpy(&transport, &Rpc::Transport::closed);

    EXPECT_FALSE(transport.isOpen());
    transport.send(QJsonObject{{"id", "dropped"}});
    EXPECT_TRUE(pair.outBytes.isEmpty());

    transport.start();
    EXPECT_TRUE(transport.isOpen());

    transport.stop();
    EXPECT_FALSE(transport.isOpen());
    EXPECT_EQ(closedSpy.size(), 1);

    transport.send(QJsonObject{{"id", "dropped-too"}});
    EXPECT_TRUE(pair.outBytes.isEmpty());

    pair.feed("{\"id\":\"late\"}\n");
    QSignalSpy received(&transport, &Rpc::Transport::messageReceived);
    pumpEventLoop(std::chrono::milliseconds(20));
    EXPECT_EQ(received.size(), 0);
}

#ifndef Q_OS_WIN
TEST_F(McpStdioServerTransportTest, ClientTransportEchoesThroughChildProcess)
{
    Rpc::StdioLaunchConfig cfg;
    cfg.program = QStringLiteral("/bin/cat");

    Rpc::StdioClientTransport transport(cfg);
    QSignalSpy received(&transport, &Rpc::Transport::messageReceived);
    QSignalSpy closedSpy(&transport, &Rpc::Transport::closed);

    transport.start();
    ASSERT_TRUE(transport.isOpen());

    const QJsonObject request{{"jsonrpc", "2.0"}, {"id", "1"}, {"method", "ping"}};
    transport.send(request);

    ASSERT_TRUE(waitForSignal(received, 1));
    EXPECT_EQ(received.at(0).at(0).toJsonObject(), request);

    transport.stop();
    EXPECT_FALSE(transport.isOpen());
    EXPECT_GE(closedSpy.size(), 1);
}
#endif
