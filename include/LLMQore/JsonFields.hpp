// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QList>
#include <QString>
#include <QStringList>

// A wire structure describes its fields once -- name, member, and what happens
// when the value is empty -- and both halves of the conversion are derived from
// that description. A struct opts in by declaring, in its own namespace,
//
//     constexpr auto jsonSchema(const Foo *)
//     {
//         return std::make_tuple(
//             LLMQore::Json::field("id", &Foo::id),
//             LLMQore::Json::omitEmpty("title", &Foo::title));
//     }
//
// found by argument-dependent lookup. A struct that also wants to carry unknown
// keys across a round-trip declares `jsonExtras(const Foo *)` returning the
// member that holds them.

namespace LLMQore::Json {

enum class Presence {
    Always,
    OmitWhenEmpty,
    NullWhenEmpty,
};

template<typename Struct, typename Member>
struct Field
{
    const char *name;
    Member Struct::*member;
    Presence presence;
};

template<typename Struct, typename Member>
constexpr Field<Struct, Member> field(const char *name, Member Struct::*member)
{
    return Field<Struct, Member>{name, member, Presence::Always};
}

template<typename Struct, typename Member>
constexpr Field<Struct, Member> omitEmpty(const char *name, Member Struct::*member)
{
    return Field<Struct, Member>{name, member, Presence::OmitWhenEmpty};
}

template<typename Struct, typename Member>
constexpr Field<Struct, Member> nullWhenEmpty(const char *name, Member Struct::*member)
{
    return Field<Struct, Member>{name, member, Presence::NullWhenEmpty};
}

namespace detail {

template<typename T>
struct IsOptional : std::false_type
{};

template<typename T>
struct IsOptional<std::optional<T>> : std::true_type
{};

template<typename T>
struct IsList : std::false_type
{};

template<typename T>
struct IsList<QList<T>> : std::true_type
{};

template<typename T, typename = void>
struct HasWireJson : std::false_type
{};

template<typename T>
struct HasWireJson<T, std::void_t<decltype(std::declval<const T &>().toJson())>> : std::true_type
{};

template<typename T, typename = void>
struct HasIsEmpty : std::false_type
{};

template<typename T>
struct HasIsEmpty<T, std::void_t<decltype(std::declval<const T &>().isEmpty())>> : std::true_type
{};

template<typename T, typename = void>
struct HasSchema : std::false_type
{};

template<typename T>
struct HasSchema<T, std::void_t<decltype(jsonSchema(static_cast<const T *>(nullptr)))>>
    : std::true_type
{};

template<typename T, typename = void>
struct HasExtras : std::false_type
{};

template<typename T>
struct HasExtras<T, std::void_t<decltype(jsonExtras(static_cast<const T *>(nullptr)))>>
    : std::true_type
{};

template<typename M>
bool isEmptyValue(const M &m)
{
    if constexpr (HasIsEmpty<M>::value)
        return m.isEmpty();
    else
        return false;
}

template<typename M>
QJsonValue encodeValue(const M &m)
{
    if constexpr (IsOptional<M>::value) {
        return m ? encodeValue(*m) : QJsonValue();
    } else if constexpr (std::is_same_v<M, QStringList>) {
        QJsonArray arr;
        for (const QString &s : m)
            arr.append(s);
        return arr;
    } else if constexpr (IsList<M>::value) {
        QJsonArray arr;
        for (const auto &item : m)
            arr.append(encodeValue(item));
        return arr;
    } else if constexpr (HasWireJson<M>::value) {
        return m.toJson();
    } else if constexpr (std::is_same_v<M, QJsonObject>) {
        return m;
    } else {
        return QJsonValue(m);
    }
}

template<typename M>
void decodeValue(const QJsonValue &v, M &m)
{
    if constexpr (IsOptional<M>::value) {
        using Inner = typename M::value_type;
        if (v.isNull() || v.isUndefined())
            return;
        if constexpr (HasWireJson<Inner>::value) {
            if (!v.isObject())
                return;
        }
        Inner inner{};
        decodeValue(v, inner);
        m = std::move(inner);
    } else if constexpr (std::is_same_v<M, QStringList>) {
        m.clear();
        const QJsonArray arr = v.toArray();
        for (const QJsonValue &item : arr)
            m.append(item.toString());
    } else if constexpr (IsList<M>::value) {
        m.clear();
        const QJsonArray arr = v.toArray();
        for (const QJsonValue &item : arr) {
            typename M::value_type entry{};
            decodeValue(item, entry);
            m.append(std::move(entry));
        }
    } else if constexpr (HasWireJson<M>::value) {
        m = M::fromJson(v.toObject());
    } else if constexpr (std::is_same_v<M, QJsonObject>) {
        m = v.toObject();
    } else if constexpr (std::is_same_v<M, QString>) {
        m = v.toString(m);
    } else if constexpr (std::is_same_v<M, bool>) {
        m = v.toBool(m);
    } else {
        m = static_cast<M>(v.toInt(m));
    }
}

template<typename T, typename S, typename M>
void writeField(QJsonObject &out, const T &value, const Field<S, M> &f)
{
    const M &m = value.*(f.member);
    const QLatin1String key(f.name);

    bool empty = isEmptyValue(m);
    if constexpr (IsOptional<M>::value)
        empty = !m.has_value();

    switch (f.presence) {
    case Presence::OmitWhenEmpty:
        if (empty)
            return;
        break;
    case Presence::NullWhenEmpty:
        if (empty) {
            out.insert(key, QJsonValue());
            return;
        }
        break;
    case Presence::Always:
        if constexpr (IsOptional<M>::value) {
            if (empty)
                return;
        }
        break;
    }

    out.insert(key, encodeValue(m));
}

template<typename T, typename S, typename M>
void readField(const QJsonObject &obj, T &value, const Field<S, M> &f)
{
    const QLatin1String key(f.name);
    if (!obj.contains(key))
        return;
    decodeValue(obj.value(key), value.*(f.member));
}

} // namespace detail

template<typename T>
QJsonObject toJson(const T &value)
{
    QJsonObject out;
    if constexpr (detail::HasExtras<T>::value)
        out = value.*jsonExtras(static_cast<const T *>(nullptr));

    std::apply(
        [&](const auto &...fields) { (detail::writeField(out, value, fields), ...); },
        jsonSchema(static_cast<const T *>(nullptr)));

    return out;
}

template<typename T>
T fromJson(const QJsonObject &obj)
{
    T value;
    const auto schema = jsonSchema(static_cast<const T *>(nullptr));

    std::apply(
        [&](const auto &...fields) { (detail::readField(obj, value, fields), ...); }, schema);

    if constexpr (detail::HasExtras<T>::value) {
        QJsonObject extras = obj;
        std::apply(
            [&](const auto &...fields) { (extras.remove(QLatin1String(fields.name)), ...); },
            schema);
        value.*jsonExtras(static_cast<const T *>(nullptr)) = extras;
    }

    return value;
}

} // namespace LLMQore::Json
