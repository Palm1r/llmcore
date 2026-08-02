// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <LLMQore/JsonFields.hpp>

// Walks a structure's field table, builds a wire object in which every declared
// field is present, and asserts the structure hands all of it back. A field the
// table declares but a hand-written half forgets shows up here as a missing or
// changed key.
//
// The sample is built from the table, so this cannot see a member that never
// made it into the table at all -- that case needs a test naming the field, as
// AcpTypes.PromptResultKeepsItsUsage does.

namespace LLMQoreTest {

template<typename T>
QJsonObject sampleObject(int seed = 1);

namespace detail {

using LLMQore::Json::detail::HasSchema;
using LLMQore::Json::detail::HasWireJson;
using LLMQore::Json::detail::IsList;
using LLMQore::Json::detail::IsOptional;

template<typename M>
QJsonValue sampleValue(int seed)
{
    if constexpr (IsOptional<M>::value) {
        return sampleValue<typename M::value_type>(seed);
    } else if constexpr (std::is_same_v<M, QStringList>) {
        return QJsonArray{QStringLiteral("s%1").arg(seed)};
    } else if constexpr (IsList<M>::value) {
        return QJsonArray{sampleValue<typename M::value_type>(seed)};
    } else if constexpr (HasWireJson<M>::value) {
        return sampleObject<M>(seed);
    } else if constexpr (std::is_same_v<M, QJsonObject>) {
        return QJsonObject{{QStringLiteral("probe"), seed}};
    } else if constexpr (std::is_same_v<M, QString>) {
        return QStringLiteral("s%1").arg(seed);
    } else if constexpr (std::is_same_v<M, bool>) {
        return true;
    } else {
        return seed;
    }
}

} // namespace detail

// A struct whose wire shape depends on a discriminator specializes this to hand
// back one concrete, valid shape.
template<typename T>
QJsonObject sampleObject(int seed)
{
    if constexpr (detail::HasSchema<T>::value) {
        QJsonObject obj;
        int next = seed;
        std::apply(
            [&](const auto &...fields) {
                ((obj.insert(
                     QLatin1String(fields.name),
                     detail::sampleValue<
                         std::decay_t<decltype(std::declval<T>().*(fields.member))>>(next++))),
                 ...);
            },
            jsonSchema(static_cast<const T *>(nullptr)));
        return obj;
    } else {
        return T{}.toJson();
    }
}

template<typename T>
void expectRoundTrip(const char *name)
{
    const QJsonObject sent = sampleObject<T>();
    const QJsonObject back = T::fromJson(sent).toJson();

    EXPECT_EQ(back, sent)
        << name << " loses a field between fromJson and toJson\n  sent: "
        << QJsonDocument(sent).toJson(QJsonDocument::Compact).constData()
        << "\n  back: " << QJsonDocument(back).toJson(QJsonDocument::Compact).constData();
}

// An unknown key must survive the round-trip on the structures that declare an
// extras member -- otherwise a spec bump silently drops the peer's new field.
template<typename T>
void expectUnknownKeySurvives(const char *name)
{
    QJsonObject sent = sampleObject<T>();
    sent.insert(QStringLiteral("fieldFromANewerSpec"), QStringLiteral("keep me"));

    const QJsonObject back = T::fromJson(sent).toJson();
    EXPECT_EQ(back.value(QStringLiteral("fieldFromANewerSpec")).toString(),
              QStringLiteral("keep me"))
        << name << " dropped a field it did not recognise";
}

} // namespace LLMQoreTest
