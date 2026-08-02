// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>

#include "clients/claude/ClaudeMessage.hpp"

using namespace LLMQore;

TEST(ClaudeMessage, InitialState)
{
    ClaudeMessage msg;
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());
    EXPECT_TRUE(msg.currentThinkingContent().isEmpty());
    EXPECT_TRUE(msg.currentRedactedThinkingContent().isEmpty());
}

TEST(ClaudeMessage, HandleTextBlock)
{
    ClaudeMessage msg;
    msg.handleContentBlockStart(0, "text", {});
    EXPECT_EQ(msg.currentBlocks().size(), 1);
    EXPECT_TRUE(std::holds_alternative<TextContent>(msg.currentBlocks()[0]));

    msg.handleContentBlockDelta(0, "text_delta", QJsonObject{{"text", "Hello "}});
    msg.handleContentBlockDelta(0, "text_delta", QJsonObject{{"text", "world"}});

    auto *textBlock = std::get_if<TextContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(textBlock, nullptr);
    EXPECT_EQ(textBlock->text, "Hello world");
}

TEST(ClaudeMessage, HandleToolUseBlock)
{
    ClaudeMessage msg;
    QJsonObject data{{"id", "tool-123"}, {"name", "read_file"}, {"input", QJsonObject{}}};
    msg.handleContentBlockStart(0, "tool_use", data);

    EXPECT_EQ(msg.currentBlocks().size(), 1);
    EXPECT_EQ(msg.currentToolUseContent().size(), 1);

    auto toolBlock = msg.currentToolUseContent()[0];
    EXPECT_EQ(toolBlock.id, "tool-123");
    EXPECT_EQ(toolBlock.name, "read_file");
}

TEST(ClaudeMessage, HandleToolUseBlock_StreamedInput)
{
    ClaudeMessage msg;
    QJsonObject data{{"id", "tool-1"}, {"name", "write"}, {"input", QJsonObject{}}};
    msg.handleContentBlockStart(0, "tool_use", data);

    msg.handleContentBlockDelta(0, "input_json_delta", QJsonObject{{"partial_json", R"({"path":)"}});
    msg.handleContentBlockDelta(0, "input_json_delta", QJsonObject{{"partial_json", R"("/tmp/f"})"}});
    msg.handleContentBlockStop(0);

    auto toolBlock = msg.currentToolUseContent()[0];
    EXPECT_EQ(toolBlock.input["path"].toString(), "/tmp/f");
}

TEST(ClaudeMessage, HandleThinkingBlock)
{
    ClaudeMessage msg;
    QJsonObject data{{"thinking", ""}, {"signature", ""}};
    msg.handleContentBlockStart(0, "thinking", data);

    msg.handleContentBlockDelta(0, "thinking_delta", QJsonObject{{"thinking", "Let me think..."}});
    msg.handleContentBlockDelta(0, "signature_delta", QJsonObject{{"signature", "sig123"}});

    auto thinkingBlocks = msg.currentThinkingContent();
    EXPECT_EQ(thinkingBlocks.size(), 1);
    EXPECT_EQ(thinkingBlocks[0].thinking, "Let me think...");
    EXPECT_EQ(thinkingBlocks[0].signature, "sig123");
}

TEST(ClaudeMessage, HandleRedactedThinkingBlock)
{
    ClaudeMessage msg;
    QJsonObject data{{"signature", "redacted-sig"}};
    msg.handleContentBlockStart(0, "redacted_thinking", data);

    auto redactedBlocks = msg.currentRedactedThinkingContent();
    EXPECT_EQ(redactedBlocks.size(), 1);
    EXPECT_EQ(redactedBlocks[0].signature, "redacted-sig");
}

TEST(ClaudeMessage, HandleStopReason_EndTurn)
{
    ClaudeMessage msg;
    msg.handleContentBlockStart(0, "text", {});
    msg.handleStopReason("end_turn");
    EXPECT_EQ(msg.state(), MessageState::Final);
}

TEST(ClaudeMessage, HandleStopReason_ToolUse)
{
    ClaudeMessage msg;
    QJsonObject data{{"id", "t1"}, {"name", "tool"}, {"input", QJsonObject{}}};
    msg.handleContentBlockStart(0, "tool_use", data);
    msg.handleStopReason("tool_use");
    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(ClaudeMessage, HandleStopReason_ToolUseWithoutToolBlocks)
{
    ClaudeMessage msg;
    msg.handleContentBlockStart(0, "text", {});
    msg.handleStopReason("tool_use");
    EXPECT_EQ(msg.state(), MessageState::Complete);
}

TEST(ClaudeMessage, HandleStopReason_Other)
{
    ClaudeMessage msg;
    msg.handleStopReason("max_tokens");
    EXPECT_EQ(msg.state(), MessageState::Complete);
}

TEST(ClaudeMessage, ToProviderFormat_TextOnly)
{
    ClaudeMessage msg;
    msg.handleContentBlockStart(0, "text", {});
    msg.handleContentBlockDelta(0, "text_delta", QJsonObject{{"text", "Hello"}});

    QJsonObject result = msg.toProviderFormat();
    EXPECT_EQ(result["role"].toString(), "assistant");
    QJsonArray content = result["content"].toArray();
    EXPECT_EQ(content.size(), 1);
    EXPECT_EQ(content[0].toObject()["type"].toString(), "text");
    EXPECT_EQ(content[0].toObject()["text"].toString(), "Hello");
}

TEST(ClaudeMessage, ToProviderFormat_MixedBlocks)
{
    ClaudeMessage msg;
    msg.handleContentBlockStart(0, "thinking", QJsonObject{{"thinking", ""}, {"signature", ""}});
    msg.handleContentBlockDelta(0, "thinking_delta", QJsonObject{{"thinking", "hmm"}});
    msg.handleContentBlockStart(1, "text", {});
    msg.handleContentBlockDelta(1, "text_delta", QJsonObject{{"text", "answer"}});

    QJsonObject result = msg.toProviderFormat();
    QJsonArray content = result["content"].toArray();
    EXPECT_EQ(content.size(), 2);
    EXPECT_EQ(content[0].toObject()["type"].toString(), "thinking");
    EXPECT_EQ(content[1].toObject()["type"].toString(), "text");
}

TEST(ClaudeMessage, CreateToolResultsContent)
{
    ClaudeMessage msg;
    QJsonObject data1{{"id", "t1"}, {"name", "read"}, {"input", QJsonObject{}}};
    QJsonObject data2{{"id", "t2"}, {"name", "write"}, {"input", QJsonObject{}}};
    msg.handleContentBlockStart(0, "tool_use", data1);
    msg.handleContentBlockStart(1, "tool_use", data2);

    QHash<QString, ToolResult> results;
    results["t1"] = ToolResult::text("file content");
    results["t2"] = ToolResult::text("write ok");

    QJsonArray toolResults = msg.createToolResultsContent(results);
    EXPECT_EQ(toolResults.size(), 2);

    bool foundT1 = false, foundT2 = false;
    for (const auto &val : toolResults) {
        QJsonObject obj = val.toObject();
        EXPECT_EQ(obj["type"].toString(), "tool_result");
        // Single-text-block fast path: content is a bare string.
        if (obj["tool_use_id"].toString() == "t1") {
            EXPECT_EQ(obj["content"].toString(), "file content");
            foundT1 = true;
        }
        if (obj["tool_use_id"].toString() == "t2") {
            EXPECT_EQ(obj["content"].toString(), "write ok");
            foundT2 = true;
        }
    }
    EXPECT_TRUE(foundT1);
    EXPECT_TRUE(foundT2);
}

TEST(ClaudeMessage, CreateToolResultsContentWithImageBlock)
{
    ClaudeMessage msg;
    QJsonObject data{{"id", "img"}, {"name", "read_image"}, {"input", QJsonObject{}}};
    msg.handleContentBlockStart(0, "tool_use", data);

    // A rich result: a description + an image block. Claude should emit a
    // tool_result whose `content` is a JSON array (not a bare string),
    // containing a text block and an image block with base64-encoded data.
    ToolResult r;
    r.content.append(TextContent{"here is the screenshot"});
    const QByteArray pngBytes = QByteArray("\x89PNG\r\n\x1a\n" "fake", 12);
    r.content.append(ImageContent::fromBytes(pngBytes, "image/png"));

    QHash<QString, ToolResult> results;
    results["img"] = r;

    QJsonArray toolResults = msg.createToolResultsContent(results);
    ASSERT_EQ(toolResults.size(), 1);

    const QJsonObject wrap = toolResults.first().toObject();
    EXPECT_EQ(wrap["type"].toString(), "tool_result");
    EXPECT_EQ(wrap["tool_use_id"].toString(), "img");
    ASSERT_TRUE(wrap["content"].isArray());

    const QJsonArray content = wrap["content"].toArray();
    ASSERT_EQ(content.size(), 2);

    // First block: text
    EXPECT_EQ(content[0].toObject()["type"].toString(), "text");
    EXPECT_EQ(content[0].toObject()["text"].toString(), "here is the screenshot");

    // Second block: image with base64 source
    const QJsonObject imgBlock = content[1].toObject();
    EXPECT_EQ(imgBlock["type"].toString(), "image");
    const QJsonObject source = imgBlock["source"].toObject();
    EXPECT_EQ(source["type"].toString(), "base64");
    EXPECT_EQ(source["media_type"].toString(), "image/png");
    EXPECT_EQ(
        QByteArray::fromBase64(source["data"].toString().toUtf8()), pngBytes);
}

TEST(ClaudeMessage, CreateToolResultsContentMarksErrors)
{
    ClaudeMessage msg;
    QJsonObject data{{"id", "err"}, {"name", "broken"}, {"input", QJsonObject{}}};
    msg.handleContentBlockStart(0, "tool_use", data);

    QHash<QString, ToolResult> results;
    results["err"] = ToolResult::error("nope");

    QJsonArray toolResults = msg.createToolResultsContent(results);
    ASSERT_EQ(toolResults.size(), 1);
    const QJsonObject wrap = toolResults.first().toObject();
    EXPECT_EQ(wrap["content"].toString(), "nope");
    EXPECT_TRUE(wrap["is_error"].toBool());
}

TEST(ClaudeMessage, StartNewContinuation)
{
    ClaudeMessage msg;
    msg.handleContentBlockStart(0, "text", {});
    msg.handleContentBlockDelta(0, "text_delta", QJsonObject{{"text", "old"}});
    msg.handleStopReason("end_turn");
    EXPECT_EQ(msg.state(), MessageState::Final);

    msg.startNewContinuation();
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
}

TEST(ClaudeMessage, HandleContentBlockDelta_OutOfBounds)
{
    ClaudeMessage msg;
    msg.handleContentBlockDelta(99, "text_delta", QJsonObject{{"text", "orphan"}});
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
}

TEST(ClaudeMessage, HandleContentBlockStop_NoToolInput)
{
    ClaudeMessage msg;
    msg.handleContentBlockStart(0, "text", {});
    msg.handleContentBlockStop(0);
}

TEST(ClaudeMessage, HandleImageBlock)
{
    ClaudeMessage msg;
    QJsonObject source{{"type", "base64"}, {"data", "abc"}, {"media_type", "image/png"}};
    QJsonObject data{{"source", source}};
    msg.handleContentBlockStart(0, "image", data);

    EXPECT_EQ(msg.currentBlocks().size(), 1);
    auto *imgBlock = std::get_if<ImageContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(imgBlock, nullptr);
    EXPECT_EQ(imgBlock->base64(), QString::fromUtf8(QByteArray::fromBase64(QByteArray("abc")).toBase64()));
    EXPECT_EQ(imgBlock->mimeType, "image/png");
    EXPECT_FALSE(imgBlock->isUrl());
}

TEST(ClaudeMessage, HandleImageBlock_Url)
{
    ClaudeMessage msg;
    QJsonObject source{{"type", "url"}, {"url", "https://example.com/photo.jpg"}};
    QJsonObject data{{"source", source}};
    msg.handleContentBlockStart(0, "image", data);

    EXPECT_EQ(msg.currentBlocks().size(), 1);
    auto *imgBlock = std::get_if<ImageContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(imgBlock, nullptr);
    EXPECT_EQ(imgBlock->url().toString(), "https://example.com/photo.jpg");
    EXPECT_TRUE(imgBlock->isUrl());
}

TEST(ClaudeMessage, HandleImageBlock_JpegMediaType)
{
    ClaudeMessage msg;
    QJsonObject source{{"type", "base64"}, {"data", "jpegbytes"}, {"media_type", "image/jpeg"}};
    QJsonObject data{{"source", source}};
    msg.handleContentBlockStart(0, "image", data);

    auto *imgBlock = std::get_if<ImageContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(imgBlock, nullptr);
    EXPECT_EQ(imgBlock->mimeType, "image/jpeg");
}

TEST(ClaudeMessage, ToProviderFormat_WithImage)
{
    ClaudeMessage msg;
    msg.handleContentBlockStart(0, "text", {});
    msg.handleContentBlockDelta(0, "text_delta", QJsonObject{{"text", "Here is an image:"}});

    const QString base64 = QString::fromUtf8(QByteArray("imgdata").toBase64());
    QJsonObject source = {{"type", "base64"}, {"data", base64}, {"media_type", "image/png"}};
    msg.handleContentBlockStart(1, "image", QJsonObject{{"source", source}});

    QJsonObject result = msg.toProviderFormat();
    QJsonArray content = result["content"].toArray();
    EXPECT_EQ(content.size(), 2);
    EXPECT_EQ(content[0].toObject()["type"].toString(), "text");
    EXPECT_EQ(content[1].toObject()["type"].toString(), "image");
    EXPECT_EQ(content[1].toObject()["source"].toObject()["data"].toString(), base64);
}

TEST(ClaudeMessage, ToProviderFormat_MultipleImages)
{
    ClaudeMessage msg;
    QJsonObject source1{{"type", "base64"}, {"data", "img1"}, {"media_type", "image/png"}};
    QJsonObject source2{{"type", "url"}, {"url", "https://example.com/img.jpg"}};
    msg.handleContentBlockStart(0, "image", QJsonObject{{"source", source1}});
    msg.handleContentBlockStart(1, "image", QJsonObject{{"source", source2}});

    QJsonObject result = msg.toProviderFormat();
    QJsonArray content = result["content"].toArray();
    EXPECT_EQ(content.size(), 2);
    EXPECT_EQ(content[0].toObject()["source"].toObject()["type"].toString(), "base64");
    EXPECT_EQ(content[1].toObject()["source"].toObject()["type"].toString(), "url");
}

TEST(ClaudeMessage, HandleMixedContent_TextImageToolUse)
{
    ClaudeMessage msg;
    msg.handleContentBlockStart(0, "text", {});
    msg.handleContentBlockDelta(0, "text_delta", QJsonObject{{"text", "Look at this:"}});

    QJsonObject source{{"type", "base64"}, {"data", "pic"}, {"media_type", "image/webp"}};
    msg.handleContentBlockStart(1, "image", QJsonObject{{"source", source}});

    QJsonObject toolData{{"id", "t1"}, {"name", "analyze"}, {"input", QJsonObject{}}};
    msg.handleContentBlockStart(2, "tool_use", toolData);

    EXPECT_EQ(msg.currentBlocks().size(), 3);
    EXPECT_TRUE(std::holds_alternative<TextContent>(msg.currentBlocks()[0]));
    EXPECT_TRUE(std::holds_alternative<ImageContent>(msg.currentBlocks()[1]));
    EXPECT_TRUE(std::holds_alternative<ToolUseContent>(msg.currentBlocks()[2]));
}
