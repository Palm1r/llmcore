// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPromise>
#include <QSignalSpy>

#include <LLMQore/BaseTool.hpp>
#include <LLMQore/ClaudeClient.hpp>
#include <LLMQore/OpenAIClient.hpp>
#include <LLMQore/ToolsManager.hpp>

#include "FakeHttpTransport.hpp"

using namespace LLMQore;
using LLMQoreTest::FakeHttpStream;
using LLMQoreTest::FakeHttpTransport;

namespace {

const int kRegisterTransportMetaTypes = []() {
    qRegisterMetaType<TokenUsage>("LLMQore::TokenUsage");
    qRegisterMetaType<CompletionInfo>("LLMQore::CompletionInfo");
    qRegisterMetaType<RequestID>("LLMQore::RequestID");
    return 0;
}();

class EchoTool : public BaseTool
{
    Q_OBJECT
public:
    explicit EchoTool(QString reply, QObject *parent = nullptr)
        : BaseTool(parent)
        , m_reply(std::move(reply))
    {}

    QString id() const override { return QStringLiteral("echo"); }
    QString displayName() const override { return QStringLiteral("Echo"); }
    QString description() const override { return QStringLiteral("Echoes a value"); }
    QJsonObject parametersSchema() const override { return QJsonObject{{"type", "object"}}; }

    QFuture<ToolResult> executeAsync(const QJsonObject &input) override
    {
        m_lastInput = input;
        QPromise<ToolResult> promise;
        QFuture<ToolResult> future = promise.future();
        promise.start();
        promise.addResult(ToolResult::text(m_reply));
        promise.finish();
        return future;
    }

    QJsonObject lastInput() const { return m_lastInput; }

private:
    QString m_reply;
    QJsonObject m_lastInput;
};

QString lastMessageRole(const QJsonObject &payload)
{
    const QJsonArray messages = payload.value("messages").toArray();
    if (messages.isEmpty())
        return {};
    return messages.last().toObject().value("role").toString();
}

} // namespace

TEST(ClaudeClientTransport, StreamingSseReachesChunkAndCompletionSignals)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    QSignalSpy chunkSpy(&client, &BaseClient::chunkReceived);
    QSignalSpy completedSpy(&client, &BaseClient::requestCompleted);
    QSignalSpy finalizedSpy(&client, &BaseClient::requestFinalized);

    const RequestID id = client.ask(QStringLiteral("hi"));

    ASSERT_EQ(transport.streamCount(), 1);
    const auto sent = transport.streamRequest(0);
    EXPECT_EQ(sent.verb, QByteArray("POST"));
    EXPECT_EQ(sent.url().toString(), QStringLiteral("http://fake.local/v1/messages"));
    EXPECT_EQ(sent.header("x-api-key"), QByteArray("sk-test"));
    EXPECT_TRUE(sent.payload().value("stream").toBool());

    transport.lastStream()->sendAll(
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"usage\":{\"input_tokens\":11,"
        "\"output_tokens\":1}}}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\","
        "\"text\":\"Hel\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\","
        "\"text\":\"lo\"}}\n\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},"
        "\"usage\":{\"output_tokens\":4}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n");

    ASSERT_EQ(chunkSpy.count(), 2);
    EXPECT_EQ(chunkSpy.at(0).at(1).toString(), QStringLiteral("Hel"));
    EXPECT_EQ(chunkSpy.at(1).at(1).toString(), QStringLiteral("lo"));

    ASSERT_EQ(completedSpy.count(), 1);
    EXPECT_EQ(completedSpy.first().at(0).toString(), id);
    EXPECT_EQ(completedSpy.first().at(1).toString(), QStringLiteral("Hello"));

    ASSERT_EQ(finalizedSpy.count(), 1);
    const auto info = finalizedSpy.first().at(1).value<CompletionInfo>();
    EXPECT_EQ(info.stopReason, QStringLiteral("end_turn"));
    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 11);
    EXPECT_EQ(info.usage->completionTokens, 4);
}

TEST(ClaudeClientTransport, HttpErrorStatusFailsRequestWithProviderMessage)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    QSignalSpy failedSpy(&client, &BaseClient::requestFailed);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    auto *stream = transport.lastStream();
    stream->sendHeaders(429, {{"Content-Type", "application/json"}});
    stream->sendChunk(R"({"error":{"type":"rate_limit_error","message":"slow down"}})");
    stream->sendFinished();

    ASSERT_EQ(failedSpy.count(), 1);
    EXPECT_EQ(
        failedSpy.first().at(1).toString(),
        QStringLiteral("HTTP 429: slow down (rate_limit_error)"));
}

TEST(ClaudeClientTransport, TransportErrorFailsRequest)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    QSignalSpy failedSpy(&client, &BaseClient::requestFailed);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendError(
        QStringLiteral("Host not found"), QNetworkReply::HostNotFoundError);

    ASSERT_EQ(failedSpy.count(), 1);
    EXPECT_EQ(failedSpy.first().at(1).toString(), QStringLiteral("Host not found"));
}

TEST(ClaudeClientTransport, CancelAbortsTheStream)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    QSignalSpy failedSpy(&client, &BaseClient::requestFailed);

    const RequestID id = client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    auto *stream = transport.lastStream();
    ASSERT_NE(stream, nullptr);
    client.cancelRequest(id);

    EXPECT_TRUE(stream->isAborted());
    ASSERT_EQ(failedSpy.count(), 1);
    EXPECT_EQ(failedSpy.first().at(1).toString(), QStringLiteral("Request cancelled"));
}

TEST(ClaudeClientTransport, ToolCallRoundTripSendsContinuation)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    auto *tool = new EchoTool(QStringLiteral("42"));
    client.tools()->addTool(tool);

    QSignalSpy toolStartedSpy(&client, &BaseClient::toolStarted);
    QSignalSpy completedSpy(&client, &BaseClient::requestCompleted);

    const RequestID id = client.ask(QStringLiteral("what is six times seven?"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"usage\":{\"input_tokens\":20,"
        "\"output_tokens\":1}}}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
        "{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"echo\",\"input\":{}}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
        "{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"value\\\":\\\"7\\\"}\"}}\n\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"},"
        "\"usage\":{\"output_tokens\":9}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n");

    ASSERT_TRUE(waitForStreams(transport, 2)) << "no continuation request was sent";
    EXPECT_EQ(completedSpy.count(), 0);

    ASSERT_EQ(toolStartedSpy.count(), 1);
    EXPECT_EQ(toolStartedSpy.first().at(2).toString(), QStringLiteral("echo"));
    EXPECT_EQ(tool->lastInput().value("value").toString(), QStringLiteral("7"));

    const QJsonObject continuation = transport.streamRequest(1).payload();
    EXPECT_TRUE(continuation.value("stream").toBool());

    const QJsonArray messages = continuation.value("messages").toArray();
    ASSERT_EQ(messages.size(), 3);
    EXPECT_EQ(messages.at(0).toObject().value("role").toString(), QStringLiteral("user"));
    EXPECT_EQ(messages.at(1).toObject().value("role").toString(), QStringLiteral("assistant"));

    const QJsonObject toolTurn = messages.at(2).toObject();
    EXPECT_EQ(toolTurn.value("role").toString(), QStringLiteral("user"));
    const QJsonArray toolContent = toolTurn.value("content").toArray();
    ASSERT_EQ(toolContent.size(), 1);
    const QJsonObject toolResult = toolContent.first().toObject();
    EXPECT_EQ(toolResult.value("type").toString(), QStringLiteral("tool_result"));
    EXPECT_EQ(toolResult.value("tool_use_id").toString(), QStringLiteral("toolu_1"));
    EXPECT_EQ(toolResult.value("content").toString(), QStringLiteral("42"));

    transport.lastStream()->sendAll(
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"usage\":{\"input_tokens\":30,"
        "\"output_tokens\":1}}}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\","
        "\"text\":\"42\"}}\n\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},"
        "\"usage\":{\"output_tokens\":3}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n");

    ASSERT_EQ(completedSpy.count(), 1);
    EXPECT_EQ(completedSpy.first().at(0).toString(), id);
    EXPECT_EQ(completedSpy.first().at(1).toString(), QStringLiteral("42"));
}

TEST(OpenAIClientTransport, StreamingSseReachesChunkAndCompletionSignals)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    QSignalSpy chunkSpy(&client, &BaseClient::chunkReceived);
    QSignalSpy completedSpy(&client, &BaseClient::requestCompleted);
    QSignalSpy finalizedSpy(&client, &BaseClient::requestFinalized);

    const RequestID id = client.ask(QStringLiteral("hi"));

    ASSERT_EQ(transport.streamCount(), 1);
    const auto sent = transport.streamRequest(0);
    EXPECT_EQ(sent.url().toString(), QStringLiteral("http://fake.local/v1/chat/completions"));
    EXPECT_EQ(sent.header("Authorization"), QByteArray("Bearer sk-test"));
    EXPECT_TRUE(sent.payload().value("stream").toBool());
    EXPECT_TRUE(
        sent.payload().value("stream_options").toObject().value("include_usage").toBool());

    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hel\"},"
        "\"finish_reason\":null}]}\n\n"
        "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"lo\"},"
        "\"finish_reason\":null}]}\n\n"
        "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":12,\"completion_tokens\":5}}\n\n"
        "data: [DONE]\n\n");

    ASSERT_EQ(chunkSpy.count(), 2);
    EXPECT_EQ(chunkSpy.at(0).at(1).toString(), QStringLiteral("Hel"));
    EXPECT_EQ(chunkSpy.at(1).at(1).toString(), QStringLiteral("lo"));

    ASSERT_EQ(completedSpy.count(), 1);
    EXPECT_EQ(completedSpy.first().at(0).toString(), id);
    EXPECT_EQ(completedSpy.first().at(1).toString(), QStringLiteral("Hello"));

    ASSERT_EQ(finalizedSpy.count(), 1);
    const auto info = finalizedSpy.first().at(1).value<CompletionInfo>();
    EXPECT_EQ(info.stopReason, QStringLiteral("stop"));
    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 12);
    EXPECT_EQ(info.usage->completionTokens, 5);
}

TEST(OpenAIClientTransport, BufferedResponseCompletesRequest)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    QSignalSpy completedSpy(&client, &BaseClient::requestCompleted);

    client.ask(QStringLiteral("hi"), RequestMode::Buffered);

    ASSERT_EQ(transport.bufferedCount(), 1);
    EXPECT_EQ(transport.bufferedRequest(0).verb, QByteArray("POST"));
    EXPECT_FALSE(transport.bufferedRequest(0).payload().value("stream").toBool());
    EXPECT_EQ(transport.streamCount(), 0);

    transport.respondToLast(200, R"({
        "choices": [{"finish_reason":"stop","message":{"content":"Hello"}}],
        "usage": {"prompt_tokens": 3, "completion_tokens": 2}
    })");

    ASSERT_TRUE(completedSpy.wait(3000));
    EXPECT_EQ(completedSpy.first().at(1).toString(), QStringLiteral("Hello"));
}

TEST(OpenAIClientTransport, BufferedTransportErrorFailsRequest)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    QSignalSpy failedSpy(&client, &BaseClient::requestFailed);

    client.ask(QStringLiteral("hi"), RequestMode::Buffered);
    ASSERT_EQ(transport.bufferedCount(), 1);

    transport.failLast(QStringLiteral("Connection refused"), QNetworkReply::ConnectionRefusedError);

    ASSERT_TRUE(failedSpy.wait(3000));
    EXPECT_EQ(failedSpy.first().at(1).toString(), QStringLiteral("Connection refused"));
}

TEST(OpenAIClientTransport, ToolCallRoundTripSendsContinuation)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    client.tools()->addTool(new EchoTool(QStringLiteral("42")));

    QSignalSpy toolResultSpy(&client, &BaseClient::toolResultReady);
    QSignalSpy completedSpy(&client, &BaseClient::requestCompleted);

    client.ask(QStringLiteral("what is six times seven?"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,"
        "\"id\":\"call_1\",\"function\":{\"name\":\"echo\",\"arguments\":\"\"}}]},"
        "\"finish_reason\":null}]}\n\n"
        "data: {\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,"
        "\"function\":{\"arguments\":\"{\\\"value\\\":\\\"7\\\"}\"}}]},"
        "\"finish_reason\":null}]}\n\n"
        "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n");

    ASSERT_TRUE(waitForStreams(transport, 2)) << "no continuation request was sent";

    ASSERT_EQ(toolResultSpy.count(), 1);
    EXPECT_EQ(toolResultSpy.first().at(3).toString(), QStringLiteral("42"));

    const QJsonObject continuation = transport.streamRequest(1).payload();
    const QJsonArray messages = continuation.value("messages").toArray();
    ASSERT_EQ(messages.size(), 3);
    EXPECT_EQ(messages.at(1).toObject().value("role").toString(), QStringLiteral("assistant"));
    EXPECT_EQ(lastMessageRole(continuation), QStringLiteral("tool"));

    const QJsonObject toolMessage = messages.last().toObject();
    EXPECT_EQ(toolMessage.value("tool_call_id").toString(), QStringLiteral("call_1"));
    EXPECT_EQ(toolMessage.value("content").toString(), QStringLiteral("42"));

    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"42\"},"
        "\"finish_reason\":null}]}\n\n"
        "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n");

    ASSERT_EQ(completedSpy.count(), 1);
    EXPECT_EQ(completedSpy.first().at(1).toString(), QStringLiteral("42"));
}

TEST(ClientTransportInjection, TimeoutSettingsReachTheInjectedTransport)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    EXPECT_EQ(client.transferTimeoutMs(), HttpTransport::DefaultTransferTimeoutMs);

    client.setTransferTimeout(4200);
    EXPECT_EQ(transport.transferTimeoutMs(), 4200);
    EXPECT_EQ(client.transferTimeoutMs(), 4200);
}

TEST(ClientTransportInjection, DefaultConstructionKeepsItsOwnTransport)
{
    ClaudeClient first;
    ClaudeClient second;

    first.setTransferTimeout(1000);
    EXPECT_EQ(first.transferTimeoutMs(), 1000);
    EXPECT_EQ(second.transferTimeoutMs(), HttpTransport::DefaultTransferTimeoutMs);
}

#include "tst_ClientTransport.moc"
