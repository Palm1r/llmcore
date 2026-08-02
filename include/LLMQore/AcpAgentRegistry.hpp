// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <LLMQore/AcpAgentConfig.hpp>
#include <LLMQore/LLMQore_global.h>

namespace LLMQore::Acp {

struct LLMQORE_EXPORT AcpAgentEntry
{
    QString id;
    QString name;
    QString description;
    AcpAgentConfig config;

    QJsonObject toJson() const;
    static AcpAgentEntry fromJson(const QJsonObject &obj);
};

class LLMQORE_EXPORT AcpAgentRegistry
{
public:
    void loadFromJson(const QJsonObject &obj);
    bool loadFromFile(const QString &path);

    bool isEmpty() const { return m_entries.isEmpty(); }
    int size() const { return m_entries.size(); }
    QStringList ids() const;
    QList<AcpAgentEntry> entries() const { return m_entries; }

    bool contains(const QString &id) const;
    std::optional<AcpAgentEntry> entry(const QString &id) const;

    std::optional<AcpAgentConfig> config(const QString &id, const QString &cwd = {}) const;

    void add(const AcpAgentEntry &entry);
    void clear() { m_entries.clear(); }

    QJsonObject toJson() const;

private:
    int indexOf(const QString &id) const;

    QList<AcpAgentEntry> m_entries;
};

} // namespace LLMQore::Acp
