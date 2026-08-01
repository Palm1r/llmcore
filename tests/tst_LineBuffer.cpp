// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QString>

#include <LLMQore/LineBuffer.hpp>

using namespace LLMQore;

TEST(LineBuffer, InitialState)
{
    LineBuffer buf;
    EXPECT_FALSE(buf.hasIncompleteData());
    EXPECT_EQ(buf.currentBuffer(), QByteArray());
}

TEST(LineBuffer, SingleCompleteLine)
{
    LineBuffer buf;
    QByteArrayList lines = buf.processData("{\"a\":1}\n");
    EXPECT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0], "{\"a\":1}");
    EXPECT_FALSE(buf.hasIncompleteData());
}

TEST(LineBuffer, MultipleCompleteLines)
{
    LineBuffer buf;
    QByteArrayList lines = buf.processData("{\"a\":1}\n{\"b\":2}\n");
    EXPECT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], "{\"a\":1}");
    EXPECT_EQ(lines[1], "{\"b\":2}");
    EXPECT_FALSE(buf.hasIncompleteData());
}

TEST(LineBuffer, IncompleteDataHeldOver)
{
    LineBuffer buf;
    QByteArrayList lines = buf.processData("{\"a\":");
    EXPECT_EQ(lines.size(), 0);
    EXPECT_TRUE(buf.hasIncompleteData());
    EXPECT_EQ(buf.currentBuffer(), "{\"a\":");
}

TEST(LineBuffer, SplitAcrossChunks)
{
    LineBuffer buf;

    QByteArrayList lines1 = buf.processData("{\"key\":");
    EXPECT_EQ(lines1.size(), 0);

    QByteArrayList lines2 = buf.processData("\"value\"}\n");
    EXPECT_EQ(lines2.size(), 1);
    EXPECT_EQ(lines2[0], "{\"key\":\"value\"}");
    EXPECT_FALSE(buf.hasIncompleteData());
}

TEST(LineBuffer, MixedCompleteAndIncomplete)
{
    LineBuffer buf;
    QByteArrayList lines = buf.processData("a\nb\npartial");
    EXPECT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], "a");
    EXPECT_EQ(lines[1], "b");
    EXPECT_TRUE(buf.hasIncompleteData());
    EXPECT_EQ(buf.currentBuffer(), "partial");
}

TEST(LineBuffer, EmptyLinesPreserved)
{
    LineBuffer buf;
    QByteArrayList lines = buf.processData("one\n\n\ntwo\n");
    EXPECT_EQ(lines.size(), 4);
    EXPECT_EQ(lines[0], "one");
    EXPECT_EQ(lines[1], "");
    EXPECT_EQ(lines[2], "");
    EXPECT_EQ(lines[3], "two");
}

TEST(LineBuffer, Clear)
{
    LineBuffer buf;
    buf.processData("partial");
    EXPECT_TRUE(buf.hasIncompleteData());

    buf.clear();
    EXPECT_FALSE(buf.hasIncompleteData());
    EXPECT_EQ(buf.currentBuffer(), QByteArray());
}

TEST(LineBuffer, EmptyInput)
{
    LineBuffer buf;
    QByteArrayList lines = buf.processData(QByteArray());
    EXPECT_EQ(lines.size(), 0);
}

TEST(LineBuffer, MultipleChunksAccumulate)
{
    LineBuffer buf;
    buf.processData("{\"");
    buf.processData("key\":");
    QByteArrayList lines = buf.processData("\"val\"}\n");
    EXPECT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0], "{\"key\":\"val\"}");
}

TEST(LineBuffer, MultiByteUtf8SurvivesEverySplitPoint)
{
    const QByteArray whole = QStringLiteral("{\"response\":\"Привет 你好 🙂\"}\n").toUtf8();

    for (int split = 1; split < whole.size(); ++split) {
        LineBuffer buf;
        buf.processData(whole.left(split));
        const QByteArrayList lines = buf.processData(whole.mid(split));

        ASSERT_EQ(lines.size(), 1) << "split at byte " << split;
        EXPECT_EQ(QString::fromUtf8(lines[0]), QStringLiteral("{\"response\":\"Привет 你好 🙂\"}"))
            << "split at byte " << split;
    }
}
