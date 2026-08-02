// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonDocument>
#include <QJsonObject>

#include <LLMQore/ContentBlocks.hpp>

using namespace LLMQore;

TEST(TurnContent, DefaultsToText)
{
    TurnContent block;
    EXPECT_TRUE(std::holds_alternative<TextContent>(block));
    EXPECT_TRUE(std::get<TextContent>(block).text.isEmpty());
}

TEST(TurnContent, HoldsEachAlternative)
{
    EXPECT_TRUE(std::holds_alternative<TextContent>(TurnContent{TextContent{"hi"}}));
    EXPECT_TRUE(std::holds_alternative<ImageContent>(
        TurnContent{ImageContent::fromBytes("bytes", "image/png")}));
    EXPECT_TRUE(std::holds_alternative<AudioContent>(TurnContent{AudioContent{"pcm", "audio/wav"}}));
    EXPECT_TRUE(
        std::holds_alternative<ToolUseContent>(TurnContent{ToolUseContent{"id", "name", {}}}));
    EXPECT_TRUE(std::holds_alternative<ThinkingContent>(TurnContent{ThinkingContent{}}));
    EXPECT_TRUE(
        std::holds_alternative<RedactedThinkingContent>(TurnContent{RedactedThinkingContent{}}));
}

TEST(TurnContent, IsCopyable)
{
    TurnContent original{TextContent{"hello"}};
    TurnContent copy = original;

    std::get<TextContent>(original).text = "changed";

    EXPECT_EQ(std::get<TextContent>(copy).text, "hello");
    EXPECT_EQ(std::get<TextContent>(original).text, "changed");
}

TEST(TurnContent, ListSurvivesReallocation)
{
    QList<TurnContent> blocks;
    blocks.append(TextContent{"first"});

    for (int i = 0; i < 128; ++i)
        blocks.append(TextContent{QString::number(i)});

    EXPECT_EQ(std::get<TextContent>(blocks.first()).text, "first");
    EXPECT_EQ(blocks.size(), 129);
}

TEST(ImageContent, InlineBytesRoundTripThroughBase64)
{
    const QByteArray raw = QByteArray::fromHex("deadbeef");
    const ImageContent image = ImageContent::fromBytes(raw, "image/png");

    EXPECT_FALSE(image.isUrl());
    EXPECT_EQ(image.bytes(), raw);
    EXPECT_EQ(image.mimeType, "image/png");
    EXPECT_EQ(ImageContent::fromBase64(image.base64(), "image/png").bytes(), raw);
    EXPECT_TRUE(image.url().isEmpty());
}

TEST(ImageContent, UrlSourceCarriesNoBytes)
{
    const ImageContent image = ImageContent::fromUrl(QUrl("https://example.com/img.png"));

    EXPECT_TRUE(image.isUrl());
    EXPECT_EQ(image.url().toString(), "https://example.com/img.png");
    EXPECT_TRUE(image.bytes().isEmpty());
    EXPECT_TRUE(image.base64().isEmpty());
}

TEST(ImageContent, Base64FactoryDecodes)
{
    const ImageContent image = ImageContent::fromBase64("aGVsbG8=", "image/jpeg");

    EXPECT_EQ(image.bytes(), QByteArray("hello"));
    EXPECT_EQ(image.mimeType, "image/jpeg");
}

TEST(ResourceContent, TextAndBlobAreDistinct)
{
    const ResourceContent text = ResourceContent::fromText("file:///a", "body", "text/plain");
    EXPECT_FALSE(text.isBlob());
    EXPECT_EQ(text.text(), "body");
    EXPECT_TRUE(text.blob().isEmpty());

    const ResourceContent blob
        = ResourceContent::fromBlob("file:///b", QByteArray("raw"), "application/octet-stream");
    EXPECT_TRUE(blob.isBlob());
    EXPECT_EQ(blob.blob(), QByteArray("raw"));
    EXPECT_TRUE(blob.text().isEmpty());
}

TEST(ToolContent, HoldsSharedMediaAlternatives)
{
    EXPECT_TRUE(std::holds_alternative<TextContent>(ToolContent{TextContent{"hi"}}));
    EXPECT_TRUE(
        std::holds_alternative<ImageContent>(ToolContent{ImageContent::fromBytes("b", "image/png")}));
    EXPECT_TRUE(std::holds_alternative<ResourceLinkContent>(
        ToolContent{ResourceLinkContent{"file:///x", "x", "", ""}}));
}

TEST(Overloaded, DispatchesToMatchingAlternative)
{
    const auto describe = [](const TurnContent &block) {
        return std::visit(
            overloaded{
                [](const TextContent &) -> QString { return "text"; },
                [](const ImageContent &) -> QString { return "image"; },
                [](const AudioContent &) -> QString { return "audio"; },
                [](const ToolUseContent &) -> QString { return "tool_use"; },
                [](const ToolResultContent &) -> QString { return "tool_result"; },
                [](const ThinkingContent &) -> QString { return "thinking"; },
                [](const RedactedThinkingContent &) -> QString { return "redacted_thinking"; }},
            block);
    };

    EXPECT_EQ(describe(TextContent{"a"}), "text");
    EXPECT_EQ(describe(ImageContent::fromBytes("b", "image/png")), "image");
    EXPECT_EQ(describe(AudioContent{"c", "audio/wav"}), "audio");
    EXPECT_EQ(describe(ToolUseContent{"id", "n", {}}), "tool_use");
    EXPECT_EQ(describe(ToolResultContent{"id", "n", {}, false, {}}), "tool_result");
    EXPECT_EQ(describe(ThinkingContent{}), "thinking");
    EXPECT_EQ(describe(RedactedThinkingContent{}), "redacted_thinking");
}

TEST(ThinkingContent, CarriesProviderContinuationTokens)
{
    ThinkingContent thinking;
    thinking.thinking = "step one";
    thinking.signature = "sig";
    thinking.itemId = "rs_123";
    thinking.encryptedContent = "encrypted";

    EXPECT_EQ(thinking.thinking, "step one");
    EXPECT_EQ(thinking.signature, "sig");
    EXPECT_EQ(thinking.itemId, "rs_123");
    EXPECT_EQ(thinking.encryptedContent, "encrypted");
    EXPECT_FALSE(thinking.notified);
}
