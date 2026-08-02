// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonDocument>

#include <LLMQore/RpcNdJsonCodec.hpp>

using namespace LLMQore;

TEST(RpcNdJsonCodecTest, EncodeIsOneCompactLine)
{
    const QByteArray line
        = Rpc::NdJsonCodec::encode(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}});

    EXPECT_TRUE(line.endsWith('\n'));
    EXPECT_EQ(line.count('\n'), 1);
    EXPECT_FALSE(line.contains(' '));

    Rpc::NdJsonCodec codec;
    const QList<QJsonObject> messages = codec.decode(line);
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.first().value("id").toInt(), 1);
}

TEST(RpcNdJsonCodecTest, DecodesSeveralMessagesFromOneChunk)
{
    Rpc::NdJsonCodec codec;
    const QList<QJsonObject> messages = codec.decode("{\"a\":1}\n{\"b\":2}\n");

    ASSERT_EQ(messages.size(), 2);
    EXPECT_EQ(messages.at(0).value("a").toInt(), 1);
    EXPECT_EQ(messages.at(1).value("b").toInt(), 2);
    EXPECT_FALSE(codec.hasIncompleteData());
}

TEST(RpcNdJsonCodecTest, MalformedLineIsDroppedAndTheStreamSurvives)
{
    Rpc::NdJsonCodec codec;
    const QList<QJsonObject> messages = codec.decode("{\"a\":1}\nnot json\n{\"b\":2}\n");

    ASSERT_EQ(messages.size(), 2) << "a peer's bad line must not take the stream down";
    EXPECT_EQ(messages.at(0).value("a").toInt(), 1);
    EXPECT_EQ(messages.at(1).value("b").toInt(), 2);
}

TEST(RpcNdJsonCodecTest, NonObjectJsonIsDropped)
{
    Rpc::NdJsonCodec codec;
    EXPECT_TRUE(codec.decode("[1,2,3]\n").isEmpty());
    EXPECT_TRUE(codec.decode("42\n").isEmpty());
    EXPECT_EQ(codec.decode("{\"a\":1}\n").size(), 1);
}

TEST(RpcNdJsonCodecTest, EmptyLinesAreSkipped)
{
    Rpc::NdJsonCodec codec;
    const QList<QJsonObject> messages = codec.decode("\n\n{\"a\":1}\n\n");

    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.first().value("a").toInt(), 1);
}

TEST(RpcNdJsonCodecTest, CrLfTerminatorIsAccepted)
{
    Rpc::NdJsonCodec codec;
    const QList<QJsonObject> messages = codec.decode("{\"a\":1}\r\n{\"b\":2}\r\n");

    ASSERT_EQ(messages.size(), 2);
    EXPECT_EQ(messages.at(0).value("a").toInt(), 1);
    EXPECT_EQ(messages.at(1).value("b").toInt(), 2);
}

TEST(RpcNdJsonCodecTest, MultiByteCharacterSplitAcrossChunks)
{
    const QJsonObject original{{"text", QStringLiteral("привет \xF0\x9F\x99\x82")}};
    const QByteArray line = Rpc::NdJsonCodec::encode(original);
    ASSERT_GT(line.size(), 8);

    const qsizetype cut = line.size() - 4;

    Rpc::NdJsonCodec codec;
    EXPECT_TRUE(codec.decode(line.left(cut)).isEmpty());
    EXPECT_TRUE(codec.hasIncompleteData());

    const QList<QJsonObject> messages = codec.decode(line.mid(cut));
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.first(), original);
    EXPECT_FALSE(codec.hasIncompleteData());
}

TEST(RpcNdJsonCodecTest, ClearDropsThePartialLine)
{
    Rpc::NdJsonCodec codec;
    EXPECT_TRUE(codec.decode("{\"a\":").isEmpty());
    ASSERT_TRUE(codec.hasIncompleteData());

    codec.clear();
    EXPECT_FALSE(codec.hasIncompleteData());
    EXPECT_TRUE(codec.decode("1}\n").isEmpty()) << "the leftover must not resurrect";
}
