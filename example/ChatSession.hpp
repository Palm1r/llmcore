// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>

#include "MessageModel.hpp"

class ChatSession : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("ChatSession is created by ChatController")

    Q_PROPERTY(MessageModel *messages READ messages CONSTANT)
    Q_PROPERTY(QStringList modelList READ modelList NOTIFY modelListChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool loadingModels READ loadingModels NOTIFY loadingModelsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList toolNames READ toolNames NOTIFY toolNamesChanged)

public:
    explicit ChatSession(QObject *parent = nullptr)
        : QObject(parent)
    {}

    MessageModel *messages() { return &m_messages; }
    QStringList modelList() const { return m_modelList; }
    bool busy() const { return m_busy; }
    bool loadingModels() const { return m_loadingModels; }
    QString status() const { return m_status; }
    QStringList toolNames() const { return m_toolNames; }

    Q_INVOKABLE virtual void send(const QString &text, const QString &model) = 0;
    Q_INVOKABLE virtual void stop() = 0;
    Q_INVOKABLE virtual void clear();

signals:
    void modelListChanged();
    void busyChanged();
    void loadingModelsChanged();
    void statusChanged();
    void toolNamesChanged();

protected:
    void setBusy(bool busy);
    void setLoadingModels(bool loading);
    void setStatus(const QString &status);
    void setModelList(const QStringList &models);
    void setToolNames(const QStringList &tools);

    MessageModel m_messages;

private:
    QStringList m_modelList;
    QStringList m_toolNames;
    QString m_status = QStringLiteral("Select a provider to start.");
    bool m_busy = false;
    bool m_loadingModels = false;
};

inline void ChatSession::clear()
{
    m_messages.clear();
}

inline void ChatSession::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

inline void ChatSession::setLoadingModels(bool loading)
{
    if (m_loadingModels == loading)
        return;
    m_loadingModels = loading;
    emit loadingModelsChanged();
}

inline void ChatSession::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

inline void ChatSession::setModelList(const QStringList &models)
{
    if (m_modelList == models)
        return;
    m_modelList = models;
    emit modelListChanged();
}

inline void ChatSession::setToolNames(const QStringList &tools)
{
    if (m_toolNames == tools)
        return;
    m_toolNames = tools;
    emit toolNamesChanged();
}
