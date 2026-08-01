// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <memory>

#include <QCoreApplication>
#include <QJsonObject>
#include <QSignalSpy>

#include <LLMQore/LineBuffer.hpp>
#include <LLMQore/LlamaCppClient.hpp>
#include <LLMQore/OpenAIClient.hpp>

#include "FakeHttpTransport.hpp"

using namespace LLMQore;
using LLMQoreTest::FakeHttpTransport;

namespace {

const int kRegisterLifecycleMetaTypes = []() {
    qRegisterMetaType<TokenUsage>("LLMQore::TokenUsage");
    qRegisterMetaType<CompletionInfo>("LLMQore::CompletionInfo");
    qRegisterMetaType<RequestID>("LLMQore::RequestID");
    return 0;
}();

} // namespace

TEST(ClientLifecycle, TransportDestroyedBeforeClientWithLiveStream)
{
    auto transport = std::make_unique<FakeHttpTransport>();
    auto client = std::make_unique<OpenAIClient>(
        "http://fake.local/v1", "sk-test", "gpt-test", transport.get());

    client->ask(QStringLiteral("hi"));
    ASSERT_EQ(transport->streamCount(), 1);

    transport.reset();
    client.reset();

    SUCCEED();
}

TEST(ClientLifecycle, ClientDestroyedWithLiveStreamReleasesHandle)
{
    FakeHttpTransport transport;
    QPointer<LLMQoreTest::FakeHttpStream> handle;

    {
        OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
        client.ask(QStringLiteral("hi"));
        ASSERT_EQ(transport.streamCount(), 1);
        handle = transport.lastStream();
        ASSERT_FALSE(handle.isNull());
    }

    EXPECT_TRUE(handle.isNull()) << "~BaseClient must release the in-flight stream handle";
}

TEST(ClientLifecycle, TransportDestroyedMidBufferedRequestFailsCleanly)
{
    auto transport = std::make_unique<FakeHttpTransport>();
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", transport.get());

    auto models = client.listModels();
    ASSERT_EQ(transport->bufferedCount(), 1);

    transport.reset();
    QCoreApplication::processEvents();

    SUCCEED();
}

TEST(ClientLifecycle, CancelFromThinkingHandlerDoesNotCorruptRequestTable)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    QSignalSpy failedSpy(&client, &BaseClient::requestFailed);

    QObject::connect(
        &client, &BaseClient::thinkingBlockReceived, &client,
        [&client](const RequestID &id, const QString &, const QString &) {
            client.cancelRequest(id);
        });

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"one \"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"two\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"answer\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n");

    EXPECT_EQ(failedSpy.count(), 1);
}

TEST(ClientLifecycle, LlamaCppKeepsUsageWhenStreamEndsWithoutBlankLine)
{
    FakeHttpTransport transport;
    LlamaCppClient client("http://fake.local", "", "local-model", &transport);

    QSignalSpy finalizedSpy(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    auto *stream = transport.lastStream();
    stream->sendHeaders();
    stream->sendChunk(
        "data: {\"choices\":[{\"delta\":{\"content\":\"answer\"}}]}\n\n"
        "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":11,\"completion_tokens\":22}}\n");
    stream->sendFinished();

    ASSERT_EQ(finalizedSpy.count(), 1);
    const auto info = finalizedSpy.at(0).at(1).value<CompletionInfo>();
    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 11);
    EXPECT_EQ(info.usage->completionTokens, 22);
}

TEST(ClientLifecycle, LineBufferDropsUnterminatedRunawayBuffer)
{
    LineBuffer buffer;
    buffer.setMaxBufferBytes(1024);

    const QByteArrayList none = buffer.processData(QByteArray(4096, 'x'));
    EXPECT_TRUE(none.isEmpty());
    EXPECT_FALSE(buffer.hasIncompleteData()) << "runaway buffer with no newline must be dropped";

    const QByteArrayList lines = buffer.processData("a\nb\n");
    EXPECT_EQ(lines, (QByteArrayList{"a", "b"}));
}
