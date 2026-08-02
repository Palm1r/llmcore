// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <LLMQore/ToolResult.hpp>

using namespace LLMQore;

// ---------- ToolResult convenience factories ----------

TEST(ToolResultTest, TextFactoryProducesSingleTextBlock)
{
    const ToolResult r = ToolResult::text("hello world");
    EXPECT_FALSE(r.isError);
    ASSERT_EQ(r.content.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextContent>(r.content.first()));
    EXPECT_EQ(std::get<TextContent>(r.content.first()).text, "hello world");
    EXPECT_EQ(r.asText(), "hello world");
}

TEST(ToolResultTest, ErrorFactorySetsIsErrorAndText)
{
    const ToolResult r = ToolResult::error("oops");
    EXPECT_TRUE(r.isError);
    ASSERT_EQ(r.content.size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextContent>(r.content.first()));
    EXPECT_EQ(std::get<TextContent>(r.content.first()).text, "oops");
}

TEST(ToolResultTest, EmptyFactoryHasNoContent)
{
    const ToolResult r = ToolResult::empty();
    EXPECT_FALSE(r.isError);
    EXPECT_TRUE(r.content.isEmpty());
    EXPECT_TRUE(r.isEmpty());
    EXPECT_TRUE(r.asText().isEmpty());
}

// ---------- asText flattening ----------

TEST(ToolResultTest, AsTextJoinsMultipleTextBlocks)
{
    ToolResult r;
    r.content.append(TextContent("Hello"));
    r.content.append(TextContent("World"));
    EXPECT_EQ(r.asText(), "Hello\nWorld");
}

TEST(ToolResultTest, AsTextSubstitutesImageBlock)
{
    ToolResult r;
    r.content.append(TextContent("Before"));
    r.content.append(ImageContent::fromBytes(QByteArray("\x01\x02", 2), "image/png"));
    r.content.append(TextContent("After"));
    const QString text = r.asText();
    EXPECT_TRUE(text.contains("Before"));
    EXPECT_TRUE(text.contains("After"));
    EXPECT_TRUE(text.contains("[image: image/png]"));
}

TEST(ToolResultTest, AsTextSubstitutesAudioBlock)
{
    ToolResult r;
    r.content.append(AudioContent(QByteArray("AUDIO", 5), "audio/wav"));
    EXPECT_EQ(r.asText(), "[audio: audio/wav]");
}

TEST(ToolResultTest, AsTextInlinesEmbeddedResourceText)
{
    ToolResult r;
    r.content.append(ResourceContent::fromText(
        "mem://note", "line1\nline2", "text/plain"));
    EXPECT_EQ(r.asText(), "line1\nline2");
}

TEST(ToolResultTest, AsTextSubstitutesEmbeddedResourceBlob)
{
    ToolResult r;
    r.content.append(
        ResourceContent::fromBlob("mem://blob", QByteArray("\x00\x01", 2)));
    EXPECT_EQ(r.asText(), "[resource: mem://blob]");
}

TEST(ToolResultTest, AsTextSubstitutesResourceLink)
{
    ToolResult r;
    r.content.append(
        ResourceLinkContent("https://example.com/a.txt", "A", "An A"));
    EXPECT_EQ(r.asText(), "[resource link: https://example.com/a.txt]");
}

// ---------- JSON round trips ----------

TEST(ToolResultTest, TextContentRoundTrip)
{
    const ToolContent original = TextContent("hi");
    const QJsonObject json = toolContentToJson(original);
    EXPECT_EQ(json.value("type").toString(), "text");
    EXPECT_EQ(json.value("text").toString(), "hi");

    const ToolContent back = toolContentFromJson(json);
    EXPECT_TRUE(std::holds_alternative<TextContent>(back));
    EXPECT_EQ(std::get<TextContent>(back).text, "hi");
}

TEST(ToolResultTest, ImageContentRoundTripUsesBase64)
{
    const QByteArray bytes("\x01\x02\xff", 3);
    const ToolContent original = ImageContent::fromBytes(bytes, "image/png");
    const QJsonObject json = toolContentToJson(original);
    EXPECT_EQ(json.value("type").toString(), "image");
    EXPECT_EQ(json.value("mimeType").toString(), "image/png");
    EXPECT_EQ(
        QByteArray::fromBase64(json.value("data").toString().toUtf8()), bytes);

    const ToolContent back = toolContentFromJson(json);
    EXPECT_TRUE(std::holds_alternative<ImageContent>(back));
    EXPECT_EQ(std::get<ImageContent>(back).bytes(), bytes);
    EXPECT_EQ(std::get<ImageContent>(back).mimeType, "image/png");
}

TEST(ToolResultTest, AudioContentRoundTrip)
{
    const QByteArray bytes("audio-bytes", 11);
    const ToolContent original = AudioContent(bytes, "audio/mpeg");
    const QJsonObject json = toolContentToJson(original);
    EXPECT_EQ(json.value("type").toString(), "audio");

    const ToolContent back = toolContentFromJson(json);
    EXPECT_TRUE(std::holds_alternative<AudioContent>(back));
    EXPECT_EQ(std::get<AudioContent>(back).data, bytes);
    EXPECT_EQ(std::get<AudioContent>(back).mimeType, "audio/mpeg");
}

TEST(ToolResultTest, EmbeddedResourceTextRoundTrip)
{
    const ToolContent original = ResourceContent::fromText(
        "mem://readme", "contents", "text/markdown");
    const QJsonObject json = toolContentToJson(original);
    EXPECT_EQ(json.value("type").toString(), "resource");
    const QJsonObject inner = json.value("resource").toObject();
    EXPECT_EQ(inner.value("uri").toString(), "mem://readme");
    EXPECT_EQ(inner.value("text").toString(), "contents");
    EXPECT_EQ(inner.value("mimeType").toString(), "text/markdown");

    const ToolContent back = toolContentFromJson(json);
    EXPECT_TRUE(std::holds_alternative<ResourceContent>(back));
    EXPECT_EQ(std::get<ResourceContent>(back).uri, "mem://readme");
    EXPECT_EQ(std::get<ResourceContent>(back).text(), "contents");
    EXPECT_EQ(std::get<ResourceContent>(back).mimeType, "text/markdown");
}

TEST(ToolResultTest, EmbeddedResourceBlobRoundTrip)
{
    const QByteArray bytes("\xFF\xD8\xFF", 3); // JPEG magic
    const ToolContent original = ResourceContent::fromBlob(
        "mem://thumb", bytes, "image/jpeg");
    const QJsonObject json = toolContentToJson(original);
    const QJsonObject inner = json.value("resource").toObject();
    EXPECT_TRUE(inner.contains("blob"));
    EXPECT_FALSE(inner.contains("text"));

    const ToolContent back = toolContentFromJson(json);
    EXPECT_TRUE(std::holds_alternative<ResourceContent>(back));
    EXPECT_EQ(std::get<ResourceContent>(back).blob(), bytes);
}

TEST(ToolResultTest, ResourceLinkRoundTrip)
{
    const ToolContent original = ResourceLinkContent(
        "file:///tmp/log.txt", "Log", "Build log", "text/plain");
    const QJsonObject json = toolContentToJson(original);
    EXPECT_EQ(json.value("type").toString(), "resource_link");
    EXPECT_EQ(json.value("uri").toString(), "file:///tmp/log.txt");
    EXPECT_EQ(json.value("name").toString(), "Log");

    const ToolContent back = toolContentFromJson(json);
    EXPECT_TRUE(std::holds_alternative<ResourceLinkContent>(back));
    EXPECT_EQ(std::get<ResourceLinkContent>(back).uri, "file:///tmp/log.txt");
    EXPECT_EQ(std::get<ResourceLinkContent>(back).name, "Log");
    EXPECT_EQ(std::get<ResourceLinkContent>(back).description, "Build log");
    EXPECT_EQ(std::get<ResourceLinkContent>(back).mimeType, "text/plain");
}

TEST(ToolResultTest, UnknownContentTypeFallsBackToTextPlaceholder)
{
    const QJsonObject obj{{"type", "weird-future-type"}};
    const ToolContent back = toolContentFromJson(obj);
    EXPECT_TRUE(std::holds_alternative<TextContent>(back));
    EXPECT_TRUE(std::get<TextContent>(back).text.contains("weird-future-type"));
}

// ---------- full ToolResult envelope round trip ----------

TEST(ToolResultTest, FullEnvelopeRoundTripPreservesEverything)
{
    ToolResult original;
    original.content.append(TextContent("summary"));
    original.content.append(
        ImageContent::fromBytes(QByteArray("png", 3), "image/png"));
    original.isError = false;
    original.structuredContent
        = QJsonObject{{"status", "ok"}, {"items", 3}};

    const QJsonObject json = original.toJson();
    EXPECT_EQ(json.value("content").toArray().size(), 2);
    EXPECT_FALSE(json.contains("isError"));
    EXPECT_EQ(json.value("structuredContent").toObject().value("status").toString(),
              "ok");

    const ToolResult back = ToolResult::fromJson(json);
    ASSERT_EQ(back.content.size(), 2);
    EXPECT_TRUE(std::holds_alternative<TextContent>(back.content.at(0)));
    EXPECT_EQ(std::get<TextContent>(back.content.at(0)).text, "summary");
    EXPECT_TRUE(std::holds_alternative<ImageContent>(back.content.at(1)));
    EXPECT_EQ(back.structuredContent.value("items").toInt(), 3);
    EXPECT_FALSE(back.isError);
}

TEST(ToolResultTest, IsErrorEnvelopeRoundTrip)
{
    const ToolResult original = ToolResult::error("something broke");
    const QJsonObject json = original.toJson();
    EXPECT_TRUE(json.value("isError").toBool());

    const ToolResult back = ToolResult::fromJson(json);
    EXPECT_TRUE(back.isError);
    EXPECT_EQ(back.asText(), "something broke");
}
