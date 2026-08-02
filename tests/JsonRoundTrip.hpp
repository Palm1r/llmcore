// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <LLMQore/JsonFields.hpp>

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
