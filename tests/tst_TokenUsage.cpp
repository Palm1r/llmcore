// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QSignalSpy>

#include <LLMQore/BaseTool.hpp>
#include <LLMQore/ClaudeClient.hpp>
#include <LLMQore/GoogleAIClient.hpp>
#include <LLMQore/LlamaCppClient.hpp>
#include <LLMQore/OllamaClient.hpp>
#include <LLMQore/OpenAIClient.hpp>
#include <LLMQore/OpenAIResponsesClient.hpp>
#include <LLMQore/ToolsManager.hpp>

#include "core/Usage.hpp"

#include "FakeHttpTransport.hpp"

using namespace LLMQore;
using LLMQoreTest::FakeHttpTransport;

namespace {
const int kRegisterMetaTypes = []() {
    qRegisterMetaType<TokenUsage>("LLMQore::TokenUsage");
    qRegisterMetaType<CompletionInfo>("LLMQore::CompletionInfo");
    qRegisterMetaType<RequestID>("LLMQore::RequestID");
    return 0;
}();
} // namespace

TEST(TokenUsage, DefaultIsInvalid)
{
    TokenUsage u;
    EXPECT_EQ(u.promptTokens, 0);
    EXPECT_EQ(u.completionTokens, 0);
    EXPECT_EQ(u.cachedPromptTokens, 0);
    EXPECT_EQ(u.reasoningTokens, 0);
    EXPECT_FALSE(u.isValid());
    EXPECT_EQ(u.totalTokens(), 0);
}

TEST(TokenUsage, ValidWithEitherPromptOrCompletion)
{
    TokenUsage onlyPrompt;
    onlyPrompt.promptTokens = 10;
    EXPECT_TRUE(onlyPrompt.isValid());

    TokenUsage onlyCompletion;
    onlyCompletion.completionTokens = 5;
    EXPECT_TRUE(onlyCompletion.isValid());
}

TEST(TokenUsage, TotalTokensSumsPromptAndCompletion)
{
    TokenUsage u;
    u.promptTokens = 100;
    u.completionTokens = 50;
    u.cachedPromptTokens = 25;
    u.reasoningTokens = 10;
    EXPECT_EQ(u.totalTokens(), 150);
}

namespace {

template<typename ClientT>
CompletionInfo streamTurn(ClientT &client, FakeHttpTransport &transport, const QByteArray &wire)
{
    QSignalSpy spy(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    if (transport.streamCount() != 1) {
        ADD_FAILURE() << "client did not open a stream";
        return {};
    }

    transport.lastStream()->sendAll(wire);

    EXPECT_EQ(spy.count(), 1);
    if (spy.isEmpty())
        return {};
    return spy.first().at(1).value<CompletionInfo>();
}

template<typename ClientT>
CompletionInfo bufferedTurn(ClientT &client, FakeHttpTransport &transport, const QByteArray &body)
{
    QSignalSpy spy(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"), RequestMode::Buffered);
    if (transport.bufferedCount() != 1) {
        ADD_FAILURE() << "client did not send a buffered request";
        return {};
    }

    transport.respondToLast(200, body);
    spy.wait(3000);

    EXPECT_EQ(spy.count(), 1);
    if (spy.isEmpty())
        return {};
    return spy.first().at(1).value<CompletionInfo>();
}

class StubTool : public BaseTool
{
    Q_OBJECT
public:
    using BaseTool::BaseTool;

    QString id() const override { return QStringLiteral("stub"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
    QString description() const override { return QStringLiteral("Returns a fixed value"); }
    QJsonObject parametersSchema() const override { return QJsonObject{{"type", "object"}}; }

    QFuture<ToolResult> executeAsync(const QJsonObject &) override
    {
        QPromise<ToolResult> promise;
        QFuture<ToolResult> future = promise.future();
        promise.start();
        promise.addResult(ToolResult::text(QStringLiteral("ok")));
        promise.finish();
        return future;
    }
};

QByteArray claudeToolUseTurn(int inputTokens, int cacheCreation, int cacheRead, int outputTokens)
{
    return QByteArray(
               "event: message_start\n"
               "data: {\"type\":\"message_start\",\"message\":{\"usage\":{\"input_tokens\":")
        + QByteArray::number(inputTokens)
        + ",\"output_tokens\":1,\"cache_read_input_tokens\":" + QByteArray::number(cacheRead)
        + ",\"cache_creation_input_tokens\":" + QByteArray::number(cacheCreation)
        + "}}}\n\n"
          "event: content_block_start\n"
          "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
          "{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"stub\",\"input\":{}}}\n\n"
          "event: content_block_stop\n"
          "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
          "event: message_delta\n"
          "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"},"
          "\"usage\":{\"output_tokens\":"
        + QByteArray::number(outputTokens)
        + "}}\n\n"
          "event: message_stop\n"
          "data: {\"type\":\"message_stop\"}\n\n";
}

QByteArray claudeTextTurn(int inputTokens, int cacheRead, int outputTokens)
{
    return QByteArray(
               "event: message_start\n"
               "data: {\"type\":\"message_start\",\"message\":{\"usage\":{\"input_tokens\":")
        + QByteArray::number(inputTokens)
        + ",\"output_tokens\":1,\"cache_read_input_tokens\":" + QByteArray::number(cacheRead)
        + "}}}\n\n"
          "event: content_block_start\n"
          "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\"}}"
          "\n\n"
          "event: content_block_delta\n"
          "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\","
          "\"text\":\"answer\"}}\n\n"
          "event: message_delta\n"
          "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},"
          "\"usage\":{\"output_tokens\":"
        + QByteArray::number(outputTokens)
        + "}}\n\n"
          "event: message_stop\n"
          "data: {\"type\":\"message_stop\"}\n\n";
}

} // namespace

TEST(TokenUsageClaude, BufferedExtractsAllFields)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    const auto info = bufferedTurn(client, transport, R"({
        "content": [{"type":"text","text":"hi"}],
        "stop_reason": "end_turn",
        "usage": {
            "input_tokens": 1500,
            "output_tokens": 250,
            "cache_read_input_tokens": 800
        }
    })");

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 1500);
    EXPECT_EQ(info.usage->completionTokens, 250);
    EXPECT_EQ(info.usage->cachedPromptTokens, 800);
    EXPECT_EQ(info.usage->reasoningTokens, 0); // Claude has no separate reasoning field here
}

TEST(TokenUsageClaude, BufferedMissingUsageLeavesNullopt)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    const auto info = bufferedTurn(client, transport, R"({
        "content": [{"type":"text","text":"hi"}],
        "stop_reason": "end_turn"
    })");

    EXPECT_FALSE(info.usage.has_value());
}

TEST(TokenUsageClaude, StreamingMessageDeltaUpdatesCumulativeOutputTokens)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    const auto info = streamTurn(
        client, transport,
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"usage\":{\"input_tokens\":300,"
        "\"output_tokens\":1,\"cache_read_input_tokens\":50}}}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\","
        "\"text\":\"hi\"}}\n\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},"
        "\"usage\":{\"output_tokens\":75}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n");

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 300);
    EXPECT_EQ(info.usage->completionTokens, 75);
    EXPECT_EQ(info.usage->cachedPromptTokens, 50);
}

TEST(TokenUsageOpenAI, BufferedExtractsBasicAndDetails)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    const auto info = bufferedTurn(client, transport, R"({
        "choices": [{"finish_reason":"stop","message":{"content":"hi"}}],
        "usage": {
            "prompt_tokens": 1200,
            "completion_tokens": 80,
            "prompt_tokens_details": {"cached_tokens": 600},
            "completion_tokens_details": {"reasoning_tokens": 30}
        }
    })");

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 1200);
    EXPECT_EQ(info.usage->completionTokens, 80);
    EXPECT_EQ(info.usage->cachedPromptTokens, 600);
    EXPECT_EQ(info.usage->reasoningTokens, 30);
}

TEST(TokenUsageOpenAI, StreamingFinalChunkWithEmptyChoicesCarriesUsage)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    const auto info = streamTurn(
        client, transport,
        "data: {\"choices\":[{\"finish_reason\":null,\"delta\":{\"content\":\"hi\"}}]}\n\n"
        "data: {\"choices\":[{\"finish_reason\":\"stop\",\"delta\":{}}]}\n\n"
        "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":42,\"completion_tokens\":7}}\n\n"
        "data: [DONE]\n\n");

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 42);
    EXPECT_EQ(info.usage->completionTokens, 7);
}

TEST(TokenUsageOpenAIResponses, BufferedExtractsDetailedUsage)
{
    FakeHttpTransport transport;
    OpenAIResponsesClient client("http://fake.local/v1", "sk-test", "o-test", &transport);

    const auto info = bufferedTurn(client, transport, R"({
        "output": [],
        "status": "completed",
        "usage": {
            "input_tokens": 2000,
            "output_tokens": 400,
            "input_tokens_details": {"cached_tokens": 1000},
            "output_tokens_details": {"reasoning_tokens": 150}
        }
    })");

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 2000);
    EXPECT_EQ(info.usage->completionTokens, 400);
    EXPECT_EQ(info.usage->cachedPromptTokens, 1000);
    EXPECT_EQ(info.usage->reasoningTokens, 150);
}

TEST(TokenUsageOpenAIResponses, StreamingResponseCompletedCarriesUsage)
{
    FakeHttpTransport transport;
    OpenAIResponsesClient client("http://fake.local/v1", "sk-test", "o-test", &transport);

    const auto info = streamTurn(
        client, transport,
        "event: response.output_text.delta\n"
        "data: {\"delta\":\"hi\"}\n\n"
        "event: response.completed\n"
        "data: {\"response\":{\"status\":\"completed\",\"usage\":"
        "{\"input_tokens\":120,\"output_tokens\":30,"
        "\"output_tokens_details\":{\"reasoning_tokens\":8}}}}\n\n");

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 120);
    EXPECT_EQ(info.usage->completionTokens, 30);
    EXPECT_EQ(info.usage->reasoningTokens, 8);
}

TEST(TokenUsageOllama, FinalDoneLineCarriesEvalCounts)
{
    FakeHttpTransport transport;
    OllamaClient client("http://fake.local", "", "llama-test", &transport);

    const auto info = streamTurn(
        client, transport,
        "{\"message\":{\"content\":\"hi\"},\"done\":false}\n"
        "{\"done\":true,\"prompt_eval_count\":256,\"eval_count\":64}\n");

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 256);
    EXPECT_EQ(info.usage->completionTokens, 64);
}

TEST(TokenUsageOllama, BufferedDelegatesToSameExtraction)
{
    FakeHttpTransport transport;
    OllamaClient client("http://fake.local", "", "llama-test", &transport);

    const auto info = bufferedTurn(
        client, transport,
        R"({"message":{"content":"hi"},"done":true,"prompt_eval_count":12,"eval_count":3})");

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 12);
    EXPECT_EQ(info.usage->completionTokens, 3);
}

TEST(TokenUsageLlamaCpp, NativeCompletionEndpointUsesTokensEvaluatedPredicted)
{
    FakeHttpTransport transport;
    LlamaCppClient client("http://fake.local", "", "local-model", &transport);

    const auto info = bufferedTurn(
        client, transport,
        R"({"content":"hi","stop":true,"tokens_evaluated":88,"tokens_predicted":22})");

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 88);
    EXPECT_EQ(info.usage->completionTokens, 22);
}

TEST(TokenUsageLlamaCpp, OpenAICompatPathUsesPromptCompletionTokens)
{
    FakeHttpTransport transport;
    LlamaCppClient client("http://fake.local", "", "local-model", &transport);

    const auto info = bufferedTurn(client, transport, R"({
        "choices": [{"finish_reason":"stop","message":{"content":"hi"}}],
        "usage": {"prompt_tokens": 33, "completion_tokens": 11}
    })");

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 33);
    EXPECT_EQ(info.usage->completionTokens, 11);
}

TEST(TokenUsageGoogleAI, UsageMetadataExtractsAllFields)
{
    FakeHttpTransport transport;
    GoogleAIClient client("http://fake.local", "AIza-test", "gemini-test", &transport);

    const auto info = bufferedTurn(client, transport, R"({
        "candidates": [{
            "content": {"parts": [{"text": "hi"}]},
            "finishReason": "STOP"
        }],
        "usageMetadata": {
            "promptTokenCount": 500,
            "candidatesTokenCount": 120,
            "cachedContentTokenCount": 200,
            "thoughtsTokenCount": 40
        }
    })");

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 500);
    EXPECT_EQ(info.usage->completionTokens, 120);
    EXPECT_EQ(info.usage->cachedPromptTokens, 200);
    EXPECT_EQ(info.usage->reasoningTokens, 40);
}

TEST(TokenUsageClaude, StreamingMessageDeltaWithoutMessageStartIsIgnored)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    const auto info = streamTurn(
        client, transport,
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},"
        "\"usage\":{\"output_tokens\":50,\"input_tokens\":100}}\n\n");

    EXPECT_FALSE(info.usage.has_value());
}

TEST(TokenUsageClaude, StreamingToolUseDoesNotResetCacheTokensAcrossTurns)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);
    client.tools()->addTool(new StubTool);

    QSignalSpy finalizedSpy(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);
    transport.lastStream()->sendAll(claudeToolUseTurn(1000, 900, 0, 40));

    ASSERT_TRUE(waitForStreams(transport, 2)) << "tool round-trip did not continue";
    EXPECT_EQ(finalizedSpy.count(), 0);

    transport.lastStream()->sendAll(claudeTextTurn(120, 1000, 60));

    ASSERT_EQ(finalizedSpy.count(), 1);
    const auto info = finalizedSpy.first().at(1).value<CompletionInfo>();
    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 1120);
    EXPECT_EQ(info.usage->completionTokens, 100);
    EXPECT_EQ(info.usage->cachedPromptTokens, 1000);
}

TEST(TokenUsageClaude, ContinuationWithoutUsageLeavesNullopt)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);
    client.tools()->addTool(new StubTool);

    QSignalSpy finalizedSpy(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);
    transport.lastStream()->sendAll(
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{}}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
        "{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"stub\",\"input\":{}}}\n\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n");

    ASSERT_TRUE(waitForStreams(transport, 2)) << "tool round-trip did not continue";

    transport.lastStream()->sendAll(
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{}}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\","
        "\"text\":\"ok\"}}\n\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n");

    ASSERT_EQ(finalizedSpy.count(), 1);
    const auto info = finalizedSpy.first().at(1).value<CompletionInfo>();
    EXPECT_FALSE(info.usage.has_value());
}

TEST(UsageSchemaParsing, ApplyToKeepsCountersTheResponseDidNotMention)
{
    TokenUsage base;
    base.promptTokens = 500;
    base.completionTokens = 10;
    base.cachedPromptTokens = 100;
    base.reasoningTokens = 7;

    UsageDelta delta;
    delta.completionTokens = 42;

    const TokenUsage merged = applyTo(delta, base);
    EXPECT_EQ(merged.completionTokens, 42);
    EXPECT_EQ(merged.promptTokens, 500);
    EXPECT_EQ(merged.cachedPromptTokens, 100) << "an unmentioned counter must survive";
    EXPECT_EQ(merged.reasoningTokens, 7) << "an unmentioned counter must survive";
}

TEST(UsageSchemaParsing, AMissingContainerYieldsAnEmptyDelta)
{
    constexpr UsageSchema schema{
        QLatin1String("usage"),
        {{}, QLatin1String("prompt_tokens")},
        {{}, QLatin1String("completion_tokens")},
        {QLatin1String("prompt_tokens_details"), QLatin1String("cached_tokens")},
        {}};

    EXPECT_TRUE(parseUsage(QJsonObject{{"choices", QJsonArray{}}}, schema).isEmpty());
    EXPECT_TRUE(parseUsage(QJsonObject{{"usage", QJsonValue::Null}}, schema).isEmpty());

    const UsageDelta nested = parseUsage(
        QJsonObject{
            {"usage",
             QJsonObject{
                 {"prompt_tokens", 12},
                 {"prompt_tokens_details", QJsonObject{{"cached_tokens", 4}}}}}},
        schema);
    ASSERT_TRUE(nested.promptTokens.has_value());
    EXPECT_EQ(*nested.promptTokens, 12);
    ASSERT_TRUE(nested.cachedPromptTokens.has_value());
    EXPECT_EQ(*nested.cachedPromptTokens, 4);
    EXPECT_FALSE(nested.completionTokens.has_value());
}

TEST(UsageSchemaParsing, GoogleLaterChunkDoesNotZeroEarlierCounters)
{
    FakeHttpTransport transport;
    GoogleAIClient client("http://fake.local", "key", "gemini-test", &transport);

    QSignalSpy finalized(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    transport.lastStream()->sendAll(
        "data: {\"usageMetadata\":{\"promptTokenCount\":40,\"candidatesTokenCount\":5,"
        "\"cachedContentTokenCount\":30,\"thoughtsTokenCount\":9},"
        "\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"a\"}]}}]}\n\n"
        "data: {\"usageMetadata\":{\"promptTokenCount\":40,\"candidatesTokenCount\":12},"
        "\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"b\"}]},"
        "\"finishReason\":\"STOP\"}]}\n\n");

    ASSERT_EQ(finalized.count(), 1);
    const auto usage = finalized.first().at(1).value<CompletionInfo>().usage;
    ASSERT_TRUE(usage.has_value());
    EXPECT_EQ(usage->completionTokens, 12);
    EXPECT_EQ(usage->cachedPromptTokens, 30) << "the second chunk omitted it, not reset it";
    EXPECT_EQ(usage->reasoningTokens, 9) << "the second chunk omitted it, not reset it";
}

TEST(UsageSchemaParsing, OpenAILaterChunkDoesNotZeroEarlierCounters)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    QSignalSpy finalized(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"delta\":{\"content\":\"a\"}}],"
        "\"usage\":{\"prompt_tokens\":80,\"completion_tokens\":1,"
        "\"prompt_tokens_details\":{\"cached_tokens\":64},"
        "\"completion_tokens_details\":{\"reasoning_tokens\":3}}}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}],"
        "\"usage\":{\"prompt_tokens\":80,\"completion_tokens\":11}}\n\n"
        "data: [DONE]\n\n");

    ASSERT_EQ(finalized.count(), 1);
    const auto usage = finalized.first().at(1).value<CompletionInfo>().usage;
    ASSERT_TRUE(usage.has_value());
    EXPECT_EQ(usage->completionTokens, 11);
    EXPECT_EQ(usage->cachedPromptTokens, 64) << "the final chunk omitted it, not reset it";
    EXPECT_EQ(usage->reasoningTokens, 3) << "the final chunk omitted it, not reset it";
}

TEST(UsageSchemaParsing, LlamaCppNativeCompletionReportsItsOwnCounters)
{
    FakeHttpTransport transport;
    LlamaCppClient client("http://fake.local", "", "local-model", &transport);

    QSignalSpy finalized(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    transport.lastStream()->sendAll(
        "data: {\"content\":\"a\",\"stop\":false,\"tokens_evaluated\":21}\n\n"
        "data: {\"content\":\"\",\"stop\":true,\"tokens_predicted\":6}\n\n");

    ASSERT_EQ(finalized.count(), 1);
    const auto usage = finalized.first().at(1).value<CompletionInfo>().usage;
    ASSERT_TRUE(usage.has_value());
    EXPECT_EQ(usage->promptTokens, 21) << "the final chunk omitted it, not reset it";
    EXPECT_EQ(usage->completionTokens, 6);
}

#include "tst_TokenUsage.moc"
