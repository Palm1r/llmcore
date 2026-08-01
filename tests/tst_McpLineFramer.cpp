// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <LLMQore/RpcLineFramer.hpp>

using namespace LLMQore;

TEST(McpLineFramerTest, SingleCompleteLine)
{
    Rpc::LineFramer framer;
    const auto lines = framer.append("{\"a\":1}\n");
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines.first(), QByteArray("{\"a\":1}"));
    EXPECT_FALSE(framer.hasIncompleteData());
}

TEST(McpLineFramerTest, MultipleLinesSingleChunk)
{
    Rpc::LineFramer framer;
    const auto lines = framer.append("{\"a\":1}\n{\"b\":2}\n{\"c\":3}\n");
    ASSERT_EQ(lines.size(), 3);
    EXPECT_EQ(lines.at(0), QByteArray("{\"a\":1}"));
    EXPECT_EQ(lines.at(1), QByteArray("{\"b\":2}"));
    EXPECT_EQ(lines.at(2), QByteArray("{\"c\":3}"));
}

TEST(McpLineFramerTest, PartialLineAcrossCalls)
{
    Rpc::LineFramer framer;
    auto lines = framer.append("{\"a\":");
    EXPECT_TRUE(lines.isEmpty());
    EXPECT_TRUE(framer.hasIncompleteData());
    lines = framer.append("1}\n");
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines.first(), QByteArray("{\"a\":1}"));
    EXPECT_FALSE(framer.hasIncompleteData());
}

TEST(McpLineFramerTest, HandlesCrLf)
{
    Rpc::LineFramer framer;
    const auto lines = framer.append("{\"a\":1}\r\n{\"b\":2}\r\n");
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines.at(0), QByteArray("{\"a\":1}"));
    EXPECT_EQ(lines.at(1), QByteArray("{\"b\":2}"));
}

TEST(McpLineFramerTest, SkipsEmptyLines)
{
    Rpc::LineFramer framer;
    const auto lines = framer.append("\n\n{\"a\":1}\n\n");
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines.first(), QByteArray("{\"a\":1}"));
}

TEST(McpLineFramerTest, ClearResetsBuffer)
{
    Rpc::LineFramer framer;
    framer.append("{\"partial");
    EXPECT_TRUE(framer.hasIncompleteData());
    framer.clear();
    EXPECT_FALSE(framer.hasIncompleteData());
    EXPECT_EQ(framer.currentBuffer(), QByteArray());
}

TEST(McpLineFramerTest, InitialState)
{
    Rpc::LineFramer framer;
    EXPECT_FALSE(framer.hasIncompleteData());
    EXPECT_EQ(framer.currentBuffer(), QByteArray());
}

TEST(McpLineFramerTest, EmptyInputYieldsNoLines)
{
    Rpc::LineFramer framer;
    EXPECT_TRUE(framer.append(QByteArray()).isEmpty());
    EXPECT_FALSE(framer.hasIncompleteData());
}

TEST(McpLineFramerTest, IncompleteTailIsReadableThroughCurrentBuffer)
{
    Rpc::LineFramer framer;
    const auto lines = framer.append("a\nb\npartial");
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines.at(0), QByteArray("a"));
    EXPECT_EQ(lines.at(1), QByteArray("b"));
    EXPECT_TRUE(framer.hasIncompleteData());
    EXPECT_EQ(framer.currentBuffer(), QByteArray("partial"));
}

TEST(McpLineFramerTest, MultipleChunksAccumulateIntoOneLine)
{
    Rpc::LineFramer framer;
    framer.append("{\"");
    framer.append("key\":");
    const auto lines = framer.append("\"val\"}\n");
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines.first(), QByteArray("{\"key\":\"val\"}"));
}

TEST(McpLineFramerTest, DropsUnterminatedRunawayBuffer)
{
    Rpc::LineFramer framer;
    framer.setMaxBufferBytes(1024);

    EXPECT_TRUE(framer.append(QByteArray(4096, 'x')).isEmpty());
    EXPECT_FALSE(framer.hasIncompleteData()) << "runaway buffer with no newline must be dropped";

    const auto lines = framer.append("a\nb\n");
    EXPECT_EQ(lines, (QByteArrayList{"a", "b"}));
}

TEST(McpLineFramerTest, MultiByteUtf8SurvivesEverySplitPoint)
{
    const QByteArray whole = QStringLiteral("{\"response\":\"Привет 你好 🙂\"}\n").toUtf8();

    for (int split = 1; split < whole.size(); ++split) {
        Rpc::LineFramer framer;
        framer.append(whole.left(split));
        const QByteArrayList lines = framer.append(whole.mid(split));

        ASSERT_EQ(lines.size(), 1) << "split at byte " << split;
        EXPECT_EQ(QString::fromUtf8(lines[0]), QStringLiteral("{\"response\":\"Привет 你好 🙂\"}"))
            << "split at byte " << split;
    }
}

TEST(McpLineFramerTest, Utf8MultibyteSplitAcrossChunks)
{
    Rpc::LineFramer framer;
    // "héllo" = h (0x68) é (0xC3 0xA9) l l o
    QByteArray first = QByteArray("{\"s\":\"h\xC3");
    QByteArray second = QByteArray("\xA9llo\"}\n");
    auto lines = framer.append(first);
    EXPECT_TRUE(lines.isEmpty());
    lines = framer.append(second);
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines.first(), QByteArray("{\"s\":\"h\xC3\xA9llo\"}"));
}
