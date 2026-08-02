// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>

#include <LLMQore/Conversation.hpp>
#include <LLMQore/GoogleAIClient.hpp>
#include <LLMQore/OllamaClient.hpp>
#include <LLMQore/OpenAIClient.hpp>
#include <LLMQore/OpenAIResponsesClient.hpp>

#include "clients/google/GoogleMessage.hpp"
#include "clients/ollama/OllamaMessage.hpp"
#include "clients/openai/OpenAIMessage.hpp"
#include "clients/openai/OpenAIResponsesMessage.hpp"

using namespace LLMQore;

namespace {

QList<TurnContent> assistantBlocks()
{
    return {
        ThinkingContent{
            .thinking = "weighing it up",
            .signature = "sig-1",
            .itemId = "rs_1",
            .encryptedContent = "enc-1"},
        TextContent{"Let me check the weather."},
        ToolUseContent{"call_1", "get_weather", QJsonObject{{"city", "Paris"}}}};
}

QList<TurnContent> userBlocks()
{
    return {
        TextContent{"What does this look like?"},
        ImageContent::fromBytes(QByteArray("\x89PNG-bytes"), "image/png")};
}

Conversation conversationWith(const QList<TurnContent> &user, const QList<TurnContent> &assistant)
{
    Conversation conversation;
    conversation.addUser(user);
    conversation.addAssistant(assistant);
    return conversation;
}

} // namespace

// The whole point of these tests is that each provider has exactly ONE
// turn-to-wire mapping. Both the in-flight continuation path and the
// Conversation replay path must go through it. Re-inlining the visitor in
// either place makes one of these fail.

TEST(TurnSerializerParity, OpenAIReplayMatchesSerializeTurn)
{
    OpenAIClient client("https://fake.local", "k", "gpt-test");

    const QJsonObject payload
        = client.buildConversationPayload(conversationWith(userBlocks(), assistantBlocks()));
    const QJsonArray messages = payload.value("messages").toArray();

    ASSERT_EQ(messages.size(), 2);
    EXPECT_EQ(
        messages[0].toObject(), OpenAIMessage::serializeTurn(TurnRole::User, userBlocks()));
    EXPECT_EQ(
        messages[1].toObject(),
        OpenAIMessage::serializeTurn(TurnRole::Assistant, assistantBlocks()));
}

TEST(TurnSerializerParity, OllamaReplayMatchesSerializeTurn)
{
    OllamaClient client("https://fake.local", {}, "llama-test");

    const QJsonObject payload
        = client.buildConversationPayload(conversationWith(userBlocks(), assistantBlocks()));
    const QJsonArray messages = payload.value("messages").toArray();

    ASSERT_EQ(messages.size(), 2);
    EXPECT_EQ(
        messages[0].toObject(), OllamaMessage::serializeTurn(TurnRole::User, userBlocks()));
    EXPECT_EQ(
        messages[1].toObject(),
        OllamaMessage::serializeTurn(TurnRole::Assistant, assistantBlocks()));
}

TEST(TurnSerializerParity, GoogleReplayMatchesSerializeTurn)
{
    GoogleAIClient client("https://fake.local", "k", "gemini-test");

    const QJsonObject payload
        = client.buildConversationPayload(conversationWith(userBlocks(), assistantBlocks()));
    const QJsonArray contents = payload.value("contents").toArray();

    ASSERT_EQ(contents.size(), 2);
    EXPECT_EQ(
        contents[0].toObject(), GoogleMessage::serializeTurn(TurnRole::User, userBlocks()));
    EXPECT_EQ(
        contents[1].toObject(),
        GoogleMessage::serializeTurn(TurnRole::Assistant, assistantBlocks()));
}

TEST(TurnSerializerParity, OpenAIResponsesReplayMatchesSerializeTurn)
{
    OpenAIResponsesClient client("https://fake.local", "k", "gpt-test");
    client.setReasoningPersistence(ReasoningPersistence::Replay);

    const QJsonObject payload
        = client.buildConversationPayload(conversationWith(userBlocks(), assistantBlocks()));
    const QJsonArray input = payload.value("input").toArray();

    QList<QJsonObject> expected = OpenAIResponsesMessage::serializeTurn(
        TurnRole::User, userBlocks(), ReasoningPersistence::Replay);
    expected += OpenAIResponsesMessage::serializeTurn(
        TurnRole::Assistant, assistantBlocks(), ReasoningPersistence::Replay);

    ASSERT_EQ(input.size(), expected.size());
    for (qsizetype i = 0; i < expected.size(); ++i)
        EXPECT_EQ(input[i].toObject(), expected[i]) << "item " << i;
}

// The Responses assistant turn must keep the model's own ordering: the narration
// it emitted before calling a tool has to precede the function_call item.
TEST(TurnSerializerParity, OpenAIResponsesKeepsTextBeforeToolCall)
{
    const QList<QJsonObject> items = OpenAIResponsesMessage::serializeTurn(
        TurnRole::Assistant,
        {TextContent{"Let me check."},
         ToolUseContent{"call_1", "get_weather", QJsonObject{}}},
        ReasoningPersistence::Off);

    ASSERT_EQ(items.size(), 2);
    EXPECT_EQ(items[0].value("role").toString(), "assistant");
    EXPECT_EQ(items[1].value("type").toString(), "function_call");
}

// An MCP tool may answer with structuredContent and no content blocks at all. The
// replay path used to render that as an empty string, so the model saw a tool call
// that returned nothing.
TEST(TurnSerializerParity, StructuredOnlyToolResultSurvivesReplay)
{
    ToolResult result;
    result.structuredContent = QJsonObject{{"temp_c", 22}};
    ASSERT_TRUE(result.content.isEmpty());

    const QString expected = toolResultText(result);
    ASSERT_FALSE(expected.isEmpty());

    Conversation conversation;
    conversation.addUser("weather?");
    conversation.addAssistant(
        QList<TurnContent>{ToolUseContent{"call_1", "get_weather", QJsonObject{}}});
    conversation.addToolResults({makeToolResultContent("call_1", "get_weather", result)});

    OpenAIClient openai("https://fake.local", "k", "gpt-test");
    const QJsonArray messages
        = openai.buildConversationPayload(conversation).value("messages").toArray();
    ASSERT_EQ(messages.size(), 3);
    EXPECT_EQ(messages[2].toObject().value("content").toString(), expected);

    OllamaClient ollama("https://fake.local", {}, "llama-test");
    const QJsonArray ollamaMessages
        = ollama.buildConversationPayload(conversation).value("messages").toArray();
    ASSERT_EQ(ollamaMessages.size(), 3);
    EXPECT_EQ(ollamaMessages[2].toObject().value("content").toString(), expected);

    GoogleAIClient google("https://fake.local", "k", "gemini-test");
    const QJsonArray contents
        = google.buildConversationPayload(conversation).value("contents").toArray();
    ASSERT_EQ(contents.size(), 3);
    EXPECT_EQ(
        contents[2]
            .toObject()
            .value("parts")
            .toArray()[0]
            .toObject()
            .value("functionResponse")
            .toObject()
            .value("response")
            .toObject()
            .value("result")
            .toString(),
        expected);
}

// An image supplied by URL used to come back from Conversation JSON as a
// ResourceLinkContent, which the providers then rendered as a text placeholder.
TEST(TurnSerializerParity, UrlImageInToolResultSurvivesJsonRoundTrip)
{
    ToolResult result;
    result.content.append(ImageContent::fromUrl(QUrl("https://example.test/a.png"), "image/png"));

    Conversation conversation;
    conversation.addUser("show me");
    conversation.addAssistant(
        QList<TurnContent>{ToolUseContent{"call_1", "get_image", QJsonObject{}}});
    conversation.addToolResults({makeToolResultContent("call_1", "get_image", result)});

    const Conversation back = Conversation::fromJson(conversation.toJson());
    ASSERT_EQ(back.turns().size(), 3);

    const auto *toolResult = std::get_if<ToolResultContent>(&back.turns()[2].content[0]);
    ASSERT_NE(toolResult, nullptr);
    ASSERT_EQ(toolResult->content.size(), 1);

    const auto *image = std::get_if<ImageContent>(&toolResult->content[0]);
    ASSERT_NE(image, nullptr) << "URL image decayed into another alternative";
    EXPECT_TRUE(image->isUrl());
    EXPECT_EQ(image->url().toString(), "https://example.test/a.png");
    EXPECT_EQ(image->mimeType, "image/png");
}

// A thought signature binds to the functionCall part that follows it. Losing that
// binding on replay makes Gemini reject the history.
TEST(TurnSerializerParity, GoogleBindsThoughtSignatureToFunctionCall)
{
    const QJsonObject turn
        = GoogleMessage::serializeTurn(TurnRole::Assistant, assistantBlocks());
    const QJsonArray parts = turn.value("parts").toArray();

    QJsonObject functionCallPart;
    for (const QJsonValue &part : parts) {
        if (part.toObject().contains("functionCall"))
            functionCallPart = part.toObject();
    }

    ASSERT_FALSE(functionCallPart.isEmpty());
    EXPECT_EQ(functionCallPart.value("thoughtSignature").toString(), "sig-1");
}
