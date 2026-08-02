// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseTool.hpp>
#include "OpenAIMessage.hpp"

#include <LLMQore/Log.hpp>

#include <QJsonArray>
#include <QJsonDocument>

namespace LLMQore {

namespace {

class OpenAIToolDialect : public ToolDialect
{
public:
    QJsonObject wrapDefinition(const BaseTool &tool) const override
    {
        return QJsonObject{
            {"type", "function"},
            {"function",
             QJsonObject{
                 {"name", tool.id()},
                 {"description", tool.description()},
                 {"parameters", tool.parametersSchema()}}}};
    }
};

} // namespace

const ToolDialect &OpenAIMessage::toolDialect()
{
    static const OpenAIToolDialect dialect;
    return dialect;
}


OpenAIMessage::ContentParts OpenAIMessage::splitContentParts(const QJsonValue &content)
{
    ContentParts out;

    if (content.isString()) {
        out.text = content.toString();
        return out;
    }
    if (!content.isArray())
        return out;

    const QJsonArray parts = content.toArray();
    for (const auto &partVal : parts) {
        const QJsonObject part = partVal.toObject();
        const QString type = part.value("type").toString();
        if (type == QLatin1String("text")) {
            out.text += part.value("text").toString();
        } else if (type == QLatin1String("thinking")) {
            const QJsonValue th = part.value("thinking");
            if (th.isString()) {
                out.thinking += th.toString();
            } else if (th.isArray()) {
                const QJsonArray thArr = th.toArray();
                for (const auto &tv : thArr) {
                    if (tv.isString()) {
                        out.thinking += tv.toString();
                    } else {
                        const QJsonObject thObj = tv.toObject();
                        if (thObj.value("type").toString() == QLatin1String("text"))
                            out.thinking += thObj.value("text").toString();
                    }
                }
            } else {
                out.thinking += part.value("text").toString();
            }
        }
    }

    return out;
}

OpenAIMessage::OpenAIMessage(QObject *parent)
    : BaseMessage(parent)
{}

void OpenAIMessage::handleContentDelta(const QString &content)
{
    auto textContent = getOrCreateTextContent();
    textContent->appendText(content);
}

void OpenAIMessage::handleReasoningDelta(const QString &reasoning)
{
    auto *thinkingContent = getOrCreateThinkingContent();
    thinkingContent->appendThinking(reasoning);
}

void OpenAIMessage::handleToolCallStart(int index, const QString &id, const QString &name)
{
    qCDebug(llmOpenAILog).noquote()
        << QString("handleToolCallStart index=%1, id=%2, name=%3").arg(index).arg(id, name);

    auto *toolContent = addCurrentContent<ToolUseContent>(id, name);
    m_toolCallByIndex[index] = toolContent;
    m_pendingToolArguments[index] = "";
}

void OpenAIMessage::handleToolCallDelta(int index, const QString &argumentsDelta)
{
    if (m_pendingToolArguments.contains(index)) {
        m_pendingToolArguments[index] += argumentsDelta;
    }
}

void OpenAIMessage::handleToolCallComplete(int index)
{
    if (!m_pendingToolArguments.contains(index))
        return;

    QString jsonArgs = m_pendingToolArguments.take(index);
    QJsonObject argsObject;

    if (!jsonArgs.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(jsonArgs.toUtf8());
        if (doc.isObject())
            argsObject = doc.object();
    }

    if (auto *toolContent = m_toolCallByIndex.value(index))
        toolContent->setInput(argsObject);

    m_toolCallByIndex.remove(index);
}

void OpenAIMessage::completeAllPendingToolCalls()
{
    const auto indices = m_pendingToolArguments.keys();
    for (int index : indices)
        handleToolCallComplete(index);
}

void OpenAIMessage::handleFinishReason(const QString &finishReason)
{
    m_finishReason = finishReason;
    updateStateFromFinishReason();
}

QJsonObject OpenAIMessage::toProviderFormat() const
{
    QJsonObject message;
    message["role"] = "assistant";

    QString textContent;
    QJsonArray toolCalls;

    for (const auto *block : m_currentBlocks) {
        if (const auto *text = dynamic_cast<const TextContent *>(block)) {
            textContent += text->text();
        } else if (const auto *tool = dynamic_cast<const ToolUseContent *>(block)) {
            QJsonDocument doc(tool->input());
            toolCalls.append(
                QJsonObject{
                    {"id", tool->id()},
                    {"type", "function"},
                    {"function",
                     QJsonObject{
                         {"name", tool->name()},
                         {"arguments", QString::fromUtf8(doc.toJson(QJsonDocument::Compact))}}}});
        }
    }

    if (!textContent.isEmpty()) {
        message["content"] = textContent;
    } else {
        message["content"] = QJsonValue();
    }

    if (!toolCalls.isEmpty()) {
        message["tool_calls"] = toolCalls;
    }

    return message;
}

QJsonArray OpenAIMessage::createToolResultMessages(
    const QHash<QString, ToolResult> &toolResults) const
{
    return mapToolResults(
        toolResults, [](const ToolUseContent &use, const ToolResult &r, QJsonArray &out) {
            out.append(
                QJsonObject{
                    {"role", "tool"},
                    {"tool_call_id", use.id()},
                    {"content", toolResultText(r)}});
        });
}

void OpenAIMessage::startNewContinuation()
{
    qCDebug(llmOpenAILog).noquote() << "Starting new continuation";

    m_toolCallByIndex.clear();

    BaseMessage::startNewContinuation();
    m_pendingToolArguments.clear();
    m_finishReason.clear();
    m_currentThinkingContent = nullptr;
}

ThinkingContent *OpenAIMessage::getOrCreateThinkingContent()
{
    if (m_currentThinkingContent)
        return m_currentThinkingContent;

    for (auto *block : m_currentBlocks) {
        if (auto *thinkingContent = dynamic_cast<ThinkingContent *>(block)) {
            m_currentThinkingContent = thinkingContent;
            return m_currentThinkingContent;
        }
    }

    m_currentThinkingContent = addCurrentContent<ThinkingContent>();
    return m_currentThinkingContent;
}

void OpenAIMessage::updateStateFromFinishReason()
{
    if (m_finishReason == "tool_calls" && !getCurrentToolUseContent().empty()) {
        m_state = MessageState::RequiresToolExecution;
    } else if (m_finishReason == "stop") {
        m_state = MessageState::Final;
    } else {
        m_state = MessageState::Complete;
    }
}

} // namespace LLMQore
