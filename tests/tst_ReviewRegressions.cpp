// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QPromise>
#include <QSignalSpy>

#include <LLMQore/BaseTool.hpp>
#include <LLMQore/ClaudeClient.hpp>
#include <LLMQore/GoogleAIClient.hpp>
#include <LLMQore/HttpClient.hpp>
#include <LLMQore/HttpStream.hpp>
#include <LLMQore/OllamaClient.hpp>
#include <LLMQore/OpenAIResponsesClient.hpp>
#include <LLMQore/ToolsManager.hpp>

#include "clients/ollama/OllamaMessage.hpp"

#include "FakeHttpTransport.hpp"

using namespace LLMQore;
using LLMQoreTest::FakeHttpTransport;

namespace {

const int kRegisterRegressionMetaTypes = []() {
    qRegisterMetaType<TokenUsage>("LLMQore::TokenUsage");
    qRegisterMetaType<CompletionInfo>("LLMQore::CompletionInfo");
    qRegisterMetaType<RequestID>("LLMQore::RequestID");
    return 0;
}();

CompletionInfo finalizedInfo(const QSignalSpy &spy, int index = 0)
{
    return spy.at(index).at(1).value<CompletionInfo>();
}

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

    QFuture<ToolResult> executeAsync(const QJsonObject &) override
    {
        QPromise<ToolResult> promise;
        QFuture<ToolResult> future = promise.future();
        promise.start();
        promise.addResult(ToolResult::text(m_reply));
        promise.finish();
        return future;
    }

private:
    QString m_reply;
};

} // namespace

// --- 1. stopReason reached only 4 of 7 providers ---

TEST(ReviewRegression, GoogleCarriesStopReasonIntoCompletionInfo)
{
    FakeHttpTransport transport;
    GoogleAIClient client("http://fake.local", "key", "gemini-test", &transport);

    QSignalSpy finalized(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "data: {\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"hello\"}]},"
        "\"finishReason\":\"STOP\"}]}\n\n");

    ASSERT_EQ(finalized.count(), 1);
    EXPECT_EQ(finalizedInfo(finalized).stopReason, QStringLiteral("STOP"))
        << "GoogleAIClient::onStreamFinished must carry the message stop reason";
}

TEST(ReviewRegression, OllamaCarriesDoneReasonIntoCompletionInfo)
{
    FakeHttpTransport transport;
    OllamaClient client("http://fake.local", "", "llama-test", &transport);

    QSignalSpy finalized(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "{\"message\":{\"content\":\"hello\"},\"done\":false}\n"
        "{\"done\":true,\"done_reason\":\"stop\"}\n");

    ASSERT_EQ(finalized.count(), 1);
    EXPECT_EQ(finalizedInfo(finalized).stopReason, QStringLiteral("stop"));
}

TEST(ReviewRegression, OpenAIResponsesCarriesStatusIntoCompletionInfo)
{
    FakeHttpTransport transport;
    OpenAIResponsesClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    QSignalSpy finalized(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "event: response.output_text.delta\n"
        "data: {\"delta\":\"hello\"}\n\n"
        "event: response.completed\n"
        "data: {\"response\":{\"status\":\"completed\"}}\n\n");

    ASSERT_EQ(finalized.count(), 1);
    EXPECT_EQ(finalizedInfo(finalized).stopReason, QStringLiteral("completed"));
}

// --- 2. HttpStream (and the QNetworkReply it adopts) had no owner ---

TEST(ReviewRegression, OpenedStreamIsOwnedByTheHttpClient)
{
    auto client = std::make_unique<HttpClient>();
    QNetworkRequest request(QUrl("http://127.0.0.1:1/never"));

    QPointer<HttpStreamHandle> stream = client->openStream(request, QByteArrayView("GET"));
    ASSERT_FALSE(stream.isNull());

    client.reset();
    EXPECT_TRUE(stream.isNull())
        << "a stream the caller never took leaks itself and the QNetworkReply it adopted";
}

TEST(ReviewRegression, StreamDeletedByTheCallerSurvivesClientTeardown)
{
    auto client = std::make_unique<HttpClient>();
    QNetworkRequest request(QUrl("http://127.0.0.1:1/never"));

    QPointer<HttpStreamHandle> stream = client->openStream(request, QByteArrayView("GET"));
    ASSERT_FALSE(stream.isNull());

    delete stream.data();
    ASSERT_TRUE(stream.isNull());

    client.reset();
    SUCCEED() << "the client must not double-delete a stream the caller already took";
}

// --- 3. tryParseToolCall left m_currentThinkingContent dangling ---

TEST(ReviewRegression, OllamaThinkingAfterToolCallDoesNotReuseFreedBlock)
{
    OllamaMessage msg;

    msg.handleThinkingDelta(QStringLiteral("first thought"));
    ASSERT_EQ(msg.getCurrentThinkingContent().size(), 1);

    msg.handleContentDelta(R"({"name":"echo","arguments":{"value":"7"}})");
    msg.handleDone(true);
    ASSERT_EQ(msg.getCurrentToolUseContent().size(), 1);
    EXPECT_TRUE(msg.getCurrentThinkingContent().isEmpty())
        << "the tool-call path deletes every accumulated block";

    msg.handleThinkingDelta(QStringLiteral("second thought"));

    ASSERT_EQ(msg.getCurrentThinkingContent().size(), 1);
    EXPECT_EQ(msg.getCurrentThinkingContent().front()->thinking(),
              QStringLiteral("second thought"))
        << "a stale m_currentThinkingContent would append into freed memory";
}

// --- 4. setResponseContent bypassed the accumulated-content signal ---

TEST(ReviewRegression, ResponsesAggregatedTextStillEmitsAccumulated)
{
    FakeHttpTransport transport;
    OpenAIResponsesClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    QSignalSpy accumulated(&client, &BaseClient::accumulatedReceived);
    QSignalSpy completed(&client, &BaseClient::requestCompleted);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "event: response.completed\n"
        "data: {\"response\":{\"status\":\"completed\","
        "\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\","
        "\"text\":\"aggregated answer\"}]}]}}\n\n");

    ASSERT_EQ(completed.count(), 1);
    EXPECT_EQ(completed.first().at(1).toString(), QStringLiteral("aggregated answer"));

    ASSERT_FALSE(accumulated.isEmpty())
        << "writing the accumulator directly leaves the UI without an update";
    EXPECT_EQ(accumulated.last().at(1).toString(), QStringLiteral("aggregated answer"));
}

// --- 5. Google sniffed raw JSON per chunk, and recorded failures for unknown ids ---

TEST(ReviewRegression, GoogleErrorBodySplitAcrossChunksStillFailsTheRequest)
{
    const QByteArray body = QJsonDocument(QJsonObject{
                                {"error",
                                 QJsonObject{{"code", 429}, {"message", "Quota exceeded"}}}})
                                .toJson(QJsonDocument::Compact);

    for (int split = 1; split < body.size(); ++split) {
        FakeHttpTransport transport;
        GoogleAIClient client("http://fake.local", "key", "gemini-test", &transport);

        QSignalSpy failed(&client, &BaseClient::requestFailed);

        client.ask(QStringLiteral("hi"));
        ASSERT_EQ(transport.streamCount(), 1) << "split at byte " << split;

        auto *stream = transport.lastStream();
        stream->sendHeaders(200);
        stream->sendChunk(body.left(split));
        stream->sendChunk(body.mid(split));
        stream->sendFinished();

        ASSERT_EQ(failed.count(), 1) << "split at byte " << split;
        EXPECT_TRUE(failed.first().at(1).toString().contains(QStringLiteral("Quota exceeded")))
            << "split at byte " << split;
    }
}

TEST(ReviewRegression, GoogleErrorAfterCancelIsNotReportedAgain)
{
    FakeHttpTransport transport;
    GoogleAIClient client("http://fake.local", "key", "gemini-test", &transport);

    const RequestID id = client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    auto *stream = transport.lastStream();
    stream->sendHeaders(200);

    client.cancelRequest(id);

    QSignalSpy failed(&client, &BaseClient::requestFailed);
    stream->sendChunk(R"({"error":{"code":429,"message":"Quota exceeded"}})");
    stream->sendFinished();

    EXPECT_EQ(failed.count(), 0)
        << "a cancelled request must not be recorded as failed after the fact";
}

TEST(ReviewRegression, GoogleSseStreamIsUnaffectedByTheErrorSniffer)
{
    FakeHttpTransport transport;
    GoogleAIClient client("http://fake.local", "key", "gemini-test", &transport);

    QSignalSpy completed(&client, &BaseClient::requestCompleted);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    auto *stream = transport.lastStream();
    stream->sendHeaders(200);
    stream->sendChunk("data: {\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"he");
    stream->sendChunk("llo\"}]},\"finishReason\":\"STOP\"}]}\n\n");
    stream->sendFinished();

    ASSERT_EQ(completed.count(), 1);
    EXPECT_EQ(completed.first().at(1).toString(), QStringLiteral("hello"));
}

// --- 6. tool round-trips were dropped before the app could keep them ---

TEST(ReviewRegression, CompletionInfoCarriesTheToolRoundTrip)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);
    client.tools()->addTool(new EchoTool(QStringLiteral("42")));

    QSignalSpy finalized(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("what is six times seven?"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{}}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
        "{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"echo\",\"input\":{}}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
        "{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"value\\\":\\\"7\\\"}\"}}\n\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n");

    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 2));

    transport.lastStream()->sendAll(
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
        "{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
        "{\"type\":\"text_delta\",\"text\":\"42\"}}\n\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n");

    ASSERT_EQ(finalized.count(), 1);
    const QJsonArray messages
        = finalizedInfo(finalized).requestPayload.value("messages").toArray();

    ASSERT_EQ(messages.size(), 3)
        << "the assistant tool_use turn and the tool_result turn must survive the loop";
    EXPECT_EQ(messages.at(0).toObject().value("role").toString(), QStringLiteral("user"));
    EXPECT_EQ(messages.at(1).toObject().value("role").toString(), QStringLiteral("assistant"));

    const QJsonArray toolContent = messages.at(2).toObject().value("content").toArray();
    ASSERT_EQ(toolContent.size(), 1);
    EXPECT_EQ(toolContent.first().toObject().value("tool_use_id").toString(),
              QStringLiteral("toolu_1"));
}

TEST(ReviewRegression, CompletionInfoCarriesThePayloadOfAToolFreeTurn)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    QSignalSpy finalized(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
        "{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
        "{\"type\":\"text_delta\",\"text\":\"hello\"}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n");

    ASSERT_EQ(finalized.count(), 1);
    const QJsonArray messages
        = finalizedInfo(finalized).requestPayload.value("messages").toArray();
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.first().toObject().value("role").toString(), QStringLiteral("user"));
}

#include "tst_ReviewRegressions.moc"
