// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonObject>
#include <QPromise>
#include <QSignalSpy>

#include <LLMQore/BaseTool.hpp>
#include <LLMQore/LlamaCppClient.hpp>
#include <LLMQore/OllamaClient.hpp>
#include <LLMQore/OpenAIClient.hpp>
#include <LLMQore/ToolsManager.hpp>

#include "FakeHttpTransport.hpp"

using namespace LLMQore;
using LLMQoreTest::FakeHttpTransport;
using LLMQoreTest::waitForStreams;

namespace {

class ThinkingEchoTool : public BaseTool
{
    Q_OBJECT
public:
    using BaseTool::BaseTool;

    QString id() const override { return QStringLiteral("echo"); }
    QString displayName() const override { return QStringLiteral("Echo"); }
    QString description() const override { return QStringLiteral("Echoes a value"); }
    QJsonObject parametersSchema() const override { return QJsonObject{{"type", "object"}}; }

    QFuture<ToolResult> executeAsync(const QJsonObject &) override
    {
        QPromise<ToolResult> promise;
        QFuture<ToolResult> future = promise.future();
        promise.start();
        promise.addResult(ToolResult::text(QStringLiteral("42")));
        promise.finish();
        return future;
    }
};

QString thinkingAt(const QSignalSpy &spy, int index)
{
    return spy.at(index).at(1).toString();
}

} // namespace

TEST(ThinkingBlocks, OpenAIEmitsReasoningOnceNotPerDelta)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    QSignalSpy thinkingSpy(&client, &BaseClient::thinkingBlockReceived);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"one \"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"two \"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"three\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"answer\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n");

    ASSERT_EQ(thinkingSpy.count(), 1) << "reasoning must be emitted once, not per delta";
    EXPECT_EQ(thinkingAt(thinkingSpy, 0), QStringLiteral("one two three"));
}

TEST(ThinkingBlocks, LlamaCppEmitsReasoningOnceNotPerDelta)
{
    FakeHttpTransport transport;
    LlamaCppClient client("http://fake.local", "", "local-model", &transport);

    QSignalSpy thinkingSpy(&client, &BaseClient::thinkingBlockReceived);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"aa \"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"bb\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"answer\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n");

    ASSERT_EQ(thinkingSpy.count(), 1);
    EXPECT_EQ(thinkingAt(thinkingSpy, 0), QStringLiteral("aa bb"));
}

TEST(ThinkingBlocks, OllamaEmitsThinkingOnce)
{
    FakeHttpTransport transport;
    OllamaClient client("http://fake.local", "", "qwen-test", &transport);

    QSignalSpy thinkingSpy(&client, &BaseClient::thinkingBlockReceived);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "{\"message\":{\"thinking\":\"pon\"},\"done\":false}\n"
        "{\"message\":{\"thinking\":\"der\"},\"done\":false}\n"
        "{\"message\":{\"content\":\"answer\"},\"done\":false}\n"
        "{\"message\":{},\"done\":true}\n");

    ASSERT_EQ(thinkingSpy.count(), 1);
    EXPECT_EQ(thinkingAt(thinkingSpy, 0), QStringLiteral("ponder"));
}

TEST(ThinkingBlocks, ReasoningAfterAToolContinuationIsStillEmitted)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    client.tools()->addTool(new ThinkingEchoTool(&client));

    QSignalSpy thinkingSpy(&client, &BaseClient::thinkingBlockReceived);
    QSignalSpy completedSpy(&client, &BaseClient::requestCompleted);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"round one\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\","
        "\"function\":{\"name\":\"echo\",\"arguments\":\"{}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n");

    ASSERT_TRUE(waitForStreams(transport, 2)) << "no continuation request was sent";
    ASSERT_EQ(thinkingSpy.count(), 1);
    EXPECT_EQ(thinkingAt(thinkingSpy, 0), QStringLiteral("round one"));

    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"round two\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"42\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n");

    ASSERT_EQ(completedSpy.count(), 1);
    ASSERT_EQ(thinkingSpy.count(), 2)
        << "the second round's reasoning was swallowed by the emitted-block counter";
    EXPECT_EQ(thinkingAt(thinkingSpy, 1), QStringLiteral("round two"));
}

TEST(ThinkingBlocks, MagistralArrayContentSplitsThinkingFromAnswerOnLlamaCpp)
{
    FakeHttpTransport transport;
    LlamaCppClient client("http://fake.local", "", "local-model", &transport);

    QSignalSpy thinkingSpy(&client, &BaseClient::thinkingBlockReceived);
    QSignalSpy completedSpy(&client, &BaseClient::requestCompleted);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"delta\":{\"content\":[{\"type\":\"thinking\","
        "\"thinking\":\"hmm\"}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":[{\"type\":\"text\","
        "\"text\":\"answer\"}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n");

    ASSERT_EQ(thinkingSpy.count(), 1);
    EXPECT_EQ(thinkingAt(thinkingSpy, 0), QStringLiteral("hmm"));
    ASSERT_EQ(completedSpy.count(), 1);
    EXPECT_EQ(completedSpy.first().at(1).toString(), QStringLiteral("answer"));
}

TEST(ThinkingBlocks, LlamaCppReadsCachedAndReasoningTokens)
{
    FakeHttpTransport transport;
    LlamaCppClient client("http://fake.local", "", "local-model", &transport);

    QSignalSpy finalizedSpy(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"delta\":{\"content\":\"answer\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":100,\"completion_tokens\":20,"
        "\"prompt_tokens_details\":{\"cached_tokens\":80},"
        "\"completion_tokens_details\":{\"reasoning_tokens\":5}}}\n\n"
        "data: [DONE]\n\n");

    ASSERT_EQ(finalizedSpy.count(), 1);
    const auto info = finalizedSpy.first().at(1).value<CompletionInfo>();
    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 100);
    EXPECT_EQ(info.usage->completionTokens, 20);
    EXPECT_EQ(info.usage->cachedPromptTokens, 80);
    EXPECT_EQ(info.usage->reasoningTokens, 5);
}

#include "tst_ThinkingBlocks.moc"
