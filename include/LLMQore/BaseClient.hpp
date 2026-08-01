// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <QFuture>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaType>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <LLMQore/HttpResponse.hpp>
#include <LLMQore/LLMQore_global.h>

#include <LLMQore/BaseMessage.hpp>
#include <LLMQore/RequestMode.hpp>
#include <LLMQore/RpcLineFramer.hpp>
#include <LLMQore/SSEParser.hpp>
#include <LLMQore/ToolResult.hpp>
#include <LLMQore/ToolDialect.hpp>

namespace LLMQore {

class HttpStreamHandle;
class HttpTransport;
class ToolLoopRunner;
class ToolsManager;

using RequestID = QString;

struct LLMQORE_EXPORT AuthScheme
{
    enum class Placement { Header, QueryParam, None };

    Placement placement = Placement::Header;
    QString name;
    QString valuePrefix;
};

struct LLMQORE_EXPORT TokenUsage
{
    int promptTokens = 0;
    int completionTokens = 0;
    int cachedPromptTokens = 0;
    int reasoningTokens = 0;

    bool isValid() const noexcept { return promptTokens > 0 || completionTokens > 0; }
    int totalTokens() const noexcept { return promptTokens + completionTokens; }
};

struct LLMQORE_EXPORT CompletionInfo
{
    QString fullText;
    QString model;
    QString stopReason;
    std::optional<TokenUsage> usage;

    // Payload of the last turn actually sent, including every tool round-trip
    // the loop added. Callers keeping their own history must carry this forward
    // instead of their original request, or the tool exchange is lost.
    QJsonObject requestPayload;
};

class LLMQORE_EXPORT BaseClient : public QObject
{
    Q_OBJECT
public:
    explicit BaseClient(QObject *parent = nullptr);
    explicit BaseClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);
    explicit BaseClient(
        const QString &url,
        const QString &apiKey,
        const QString &model,
        HttpTransport *transport,
        QObject *parent = nullptr);
    ~BaseClient() override;

    virtual RequestID sendMessage(
        const QJsonObject &payload,
        const QString &endpoint = {},
        RequestMode mode = RequestMode::Streaming)
        = 0;
    virtual RequestID ask(
        const QString &prompt, RequestMode mode = RequestMode::Streaming)
        = 0;
    virtual QFuture<QList<QString>> listModels(const QString &endpoint = {}) = 0;
    void cancelRequest(const RequestID &requestId);

    QString url() const;
    void setUrl(const QString &url);

    QString apiKey() const;
    void setApiKey(const QString &apiKey);

    QString model() const;
    void setModel(const QString &model);

    AuthScheme authScheme() const;
    void setAuthScheme(const AuthScheme &scheme);

    QHash<QString, QString> headers() const;
    void setHeader(const QString &name, const QString &value);
    void setHeaders(const QHash<QString, QString> &headers);

    ToolsManager *tools();
    bool hasTools() const noexcept;
    
    ToolLoopRunner *toolLoop();

    int maxToolContinuations() const;
    void setMaxToolContinuations(int limit);

    virtual void continueRequest(const RequestID &id, const QJsonObject &payload);
    void abortRequest(const RequestID &id, const QString &error);
    QJsonObject buildReplayContinuation(
        const RequestID &id, const QHash<QString, ToolResult> &toolResults);

    int transferTimeoutMs() const;
    void setTransferTimeout(int milliseconds);

signals:
    void chunkReceived(const LLMQore::RequestID &id, const QString &chunk);
    void accumulatedReceived(const LLMQore::RequestID &id, const QString &accumulated);
    void requestCompleted(const LLMQore::RequestID &id, const QString &fullText);
    void requestFinalized(const LLMQore::RequestID &id, const LLMQore::CompletionInfo &info);
    void requestFailed(const LLMQore::RequestID &id, const QString &error);
    void thinkingBlockReceived(
        const LLMQore::RequestID &id, const QString &thinking, const QString &signature);
    void toolStarted(
        const LLMQore::RequestID &id,
        const QString &toolId,
        const QString &toolName,
        const QJsonObject &arguments);
    void toolResultReady(
        const LLMQore::RequestID &id,
        const QString &toolId,
        const QString &toolName,
        const QString &result);

protected:
    // The provider's tool dialect, supplied by its message translator.
    virtual const ToolDialect &toolDialect() const = 0;

    virtual void processData(const RequestID &id, const QByteArray &data) = 0;
    virtual void processBufferedResponse(const RequestID &id, const QByteArray &data) = 0;
    virtual QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults)
        = 0;

    // Per-request state beyond the message object. Only providers that keep
    // some survives-the-turn bookkeeping need this.
    virtual void cleanupDerivedData(const RequestID &id);

    // Which category the shared code logs under, so each provider still
    // reports under its own name.
    [[nodiscard]] virtual const QLoggingCategory &logCategory() const;

    [[nodiscard]] virtual QString parseHttpError(const HttpResponse &response) const;

    // "HTTP <status>: <error.message>", plus one parenthesised clause per
    // annotation whose field is present. An empty label renders the value bare.
    struct ErrorAnnotation
    {
        QString label;
        QString field;
    };
    [[nodiscard]] QString parseErrorObject(
        const HttpResponse &response, const QList<ErrorAnnotation> &annotations) const;

    // GET `url`, read `arrayKey` out of the response object, and collect
    // `idKey` from each entry. `idMapper` post-processes each raw id.
    [[nodiscard]] QFuture<QList<QString>> fetchModelList(
        const QUrl &url,
        const QString &arrayKey = QStringLiteral("data"),
        const QString &idKey = QStringLiteral("id"),
        const std::function<QString(QString)> &idMapper = {});

    [[nodiscard]] QUrl endpointUrl(const QString &endpoint, const QString &defaultPath) const;

    // --- per-request message object, owned by the base ---

    [[nodiscard]] BaseMessage *messageForRequest(const RequestID &id) const;
    void setMessageForRequest(const RequestID &id, BaseMessage *message);

    // The lazy-create-or-restart skeleton every provider's stream handler runs.
    template<typename T>
    T *ensureMessage(const RequestID &id)
    {
        if (auto *existing = qobject_cast<T *>(messageForRequest(id))) {
            if (existing->state() == MessageState::RequiresToolExecution)
                existing->startNewContinuation();
            return existing;
        }
        auto *created = new T(this);
        setMessageForRequest(id, created);
        return created;
    }

    template<typename T>
    [[nodiscard]] T *messageAs(const RequestID &id) const
    {
        return qobject_cast<T *>(messageForRequest(id));
    }

    [[nodiscard]] static QJsonObject appendChatMessagesContinuation(
        const QJsonObject &originalPayload,
        const QJsonObject &assistantMessage,
        const QJsonArray &toolMessages);

    virtual void onStreamFinished(const RequestID &id, std::optional<QString> error);

    [[nodiscard]] HttpTransport *transport() const;
    [[nodiscard]] QNetworkRequest prepareNetworkRequest(const QUrl &url) const;
    [[nodiscard]] RequestID createRequest();
    void sendRequest(
        const RequestID &id,
        const QUrl &url,
        const QJsonObject &payload,
        RequestMode mode = RequestMode::Streaming);

    void addChunk(const RequestID &id, const QString &chunk);
    void completeRequest(const RequestID &id);
    void failRequest(const RequestID &id, const QString &error);

    void captureStopReason(const RequestID &id);

    void setUsage(const RequestID &id, const TokenUsage &usage);
    void accumulateUsage(const RequestID &id, const TokenUsage &delta);
    std::optional<TokenUsage> currentUsage(const RequestID &id) const;
    std::optional<TokenUsage> totalUsage(const RequestID &id) const;
    void finalizeTurn(const RequestID &id);

    void executeToolsFromMessage(const RequestID &id);
    void cleanupFullRequest(const RequestID &id);
    void notifyPendingThinkingBlocks(const RequestID &id);

    void storeRequestContext(const RequestID &id, const QUrl &url, const QJsonObject &payload);

    bool hasRequest(const RequestID &id) const noexcept;
    Rpc::LineFramer &requestLineFramer(const RequestID &id);
    SSEParser &requestSSEParser(const RequestID &id);
    QString responseContent(const RequestID &id) const;
    void setResponseContent(const RequestID &id, const QString &content);

    QString m_url;
    QString m_apiKey;
    QString m_model;

private:
    void cleanupRequest(const RequestID &id);
    void startHttpRequest(
        const RequestID &id,
        const QNetworkRequest &request,
        const QJsonObject &payload,
        RequestMode mode);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LLMQore

Q_DECLARE_METATYPE(LLMQore::TokenUsage)
Q_DECLARE_METATYPE(LLMQore::CompletionInfo)
