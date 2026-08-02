// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/JsonFields.hpp>
#include <LLMQore/McpTypes.hpp>

// The MCP wire tables: every field of every symmetric structure, named once.
// `toJson`/`fromJson` are derived from these, and the round-trip test walks
// them, so a field added here is a field the tests already cover.

namespace LLMQore::Mcp {

using LLMQore::Json::field;
using LLMQore::Json::omitEmpty;

constexpr auto jsonSchema(const IconInfo *)
{
    return std::make_tuple(
        field("src", &IconInfo::src),
        omitEmpty("mimeType", &IconInfo::mimeType),
        omitEmpty("sizes", &IconInfo::sizes));
}

constexpr auto jsonSchema(const Implementation *)
{
    return std::make_tuple(
        field("name", &Implementation::name),
        field("version", &Implementation::version),
        omitEmpty("description", &Implementation::description),
        omitEmpty("title", &Implementation::title),
        omitEmpty("icons", &Implementation::icons));
}

constexpr auto jsonSchema(const ToolsCapability *)
{
    return std::make_tuple(omitEmpty("listChanged", &ToolsCapability::listChanged));
}

constexpr auto jsonSchema(const ResourcesCapability *)
{
    return std::make_tuple(
        omitEmpty("subscribe", &ResourcesCapability::subscribe),
        omitEmpty("listChanged", &ResourcesCapability::listChanged));
}

constexpr auto jsonSchema(const PromptsCapability *)
{
    return std::make_tuple(omitEmpty("listChanged", &PromptsCapability::listChanged));
}

// Presence-only capabilities: the empty object *is* the whole message, so the
// `present` flag has no wire field to be named by.
constexpr auto jsonSchema(const LoggingCapability *)
{
    return std::make_tuple();
}

constexpr auto jsonSchema(const CompletionsCapability *)
{
    return std::make_tuple();
}

constexpr auto jsonSchema(const SamplingCapability *)
{
    return std::make_tuple();
}

constexpr auto jsonSchema(const ElicitationCapability *)
{
    return std::make_tuple();
}

constexpr auto jsonSchema(const RootsCapability *)
{
    return std::make_tuple(omitEmpty("listChanged", &RootsCapability::listChanged));
}

constexpr auto jsonSchema(const ServerCapabilities *)
{
    return std::make_tuple(
        field("tools", &ServerCapabilities::tools),
        field("resources", &ServerCapabilities::resources),
        field("prompts", &ServerCapabilities::prompts),
        field("logging", &ServerCapabilities::logging),
        field("completions", &ServerCapabilities::completions));
}

constexpr auto jsonExtras(const ServerCapabilities *)
{
    return &ServerCapabilities::extras;
}

constexpr auto jsonSchema(const ClientCapabilities *)
{
    return std::make_tuple(
        field("roots", &ClientCapabilities::roots),
        field("sampling", &ClientCapabilities::sampling),
        field("elicitation", &ClientCapabilities::elicitation));
}

constexpr auto jsonExtras(const ClientCapabilities *)
{
    return &ClientCapabilities::extras;
}

constexpr auto jsonSchema(const InitializeResult *)
{
    return std::make_tuple(
        field("protocolVersion", &InitializeResult::protocolVersion),
        field("capabilities", &InitializeResult::capabilities),
        field("serverInfo", &InitializeResult::serverInfo),
        omitEmpty("instructions", &InitializeResult::instructions));
}

constexpr auto jsonSchema(const ToolInfo *)
{
    return std::make_tuple(
        field("name", &ToolInfo::name),
        omitEmpty("title", &ToolInfo::title),
        omitEmpty("description", &ToolInfo::description),
        field("inputSchema", &ToolInfo::inputSchema),
        omitEmpty("outputSchema", &ToolInfo::outputSchema),
        omitEmpty("annotations", &ToolInfo::annotations),
        omitEmpty("icons", &ToolInfo::icons),
        omitEmpty("_meta", &ToolInfo::meta));
}

constexpr auto jsonSchema(const ResourceInfo *)
{
    return std::make_tuple(
        field("uri", &ResourceInfo::uri),
        omitEmpty("name", &ResourceInfo::name),
        omitEmpty("title", &ResourceInfo::title),
        omitEmpty("description", &ResourceInfo::description),
        omitEmpty("mimeType", &ResourceInfo::mimeType),
        omitEmpty("icons", &ResourceInfo::icons),
        omitEmpty("_meta", &ResourceInfo::meta));
}

constexpr auto jsonSchema(const ResourceTemplate *)
{
    return std::make_tuple(
        field("uriTemplate", &ResourceTemplate::uriTemplate),
        omitEmpty("name", &ResourceTemplate::name),
        omitEmpty("title", &ResourceTemplate::title),
        omitEmpty("description", &ResourceTemplate::description),
        omitEmpty("mimeType", &ResourceTemplate::mimeType),
        omitEmpty("icons", &ResourceTemplate::icons),
        omitEmpty("_meta", &ResourceTemplate::meta));
}

constexpr auto jsonSchema(const PromptArgument *)
{
    return std::make_tuple(
        field("name", &PromptArgument::name),
        omitEmpty("description", &PromptArgument::description),
        omitEmpty("required", &PromptArgument::required));
}

constexpr auto jsonSchema(const PromptInfo *)
{
    return std::make_tuple(
        field("name", &PromptInfo::name),
        omitEmpty("title", &PromptInfo::title),
        omitEmpty("description", &PromptInfo::description),
        omitEmpty("arguments", &PromptInfo::arguments),
        omitEmpty("icons", &PromptInfo::icons),
        omitEmpty("_meta", &PromptInfo::meta));
}

constexpr auto jsonSchema(const PromptMessage *)
{
    return std::make_tuple(
        field("role", &PromptMessage::role), field("content", &PromptMessage::content));
}

constexpr auto jsonSchema(const PromptGetResult *)
{
    return std::make_tuple(
        omitEmpty("description", &PromptGetResult::description),
        field("messages", &PromptGetResult::messages));
}

constexpr auto jsonSchema(const Root *)
{
    return std::make_tuple(field("uri", &Root::uri), omitEmpty("name", &Root::name));
}

constexpr auto jsonSchema(const CompletionReference *)
{
    return std::make_tuple(
        field("type", &CompletionReference::type),
        omitEmpty("name", &CompletionReference::name),
        omitEmpty("uri", &CompletionReference::uri));
}

constexpr auto jsonSchema(const CompletionArgument *)
{
    return std::make_tuple(
        field("name", &CompletionArgument::name), field("value", &CompletionArgument::value));
}

constexpr auto jsonSchema(const SamplingMessage *)
{
    return std::make_tuple(
        field("role", &SamplingMessage::role), field("content", &SamplingMessage::content));
}

constexpr auto jsonSchema(const ModelHint *)
{
    return std::make_tuple(omitEmpty("name", &ModelHint::name));
}

constexpr auto jsonSchema(const ModelPreferences *)
{
    return std::make_tuple(
        omitEmpty("hints", &ModelPreferences::hints),
        field("costPriority", &ModelPreferences::costPriority),
        field("speedPriority", &ModelPreferences::speedPriority),
        field("intelligencePriority", &ModelPreferences::intelligencePriority));
}

constexpr auto jsonSchema(const CreateMessageParams *)
{
    return std::make_tuple(
        field("messages", &CreateMessageParams::messages),
        field("modelPreferences", &CreateMessageParams::modelPreferences),
        omitEmpty("systemPrompt", &CreateMessageParams::systemPrompt),
        omitEmpty("includeContext", &CreateMessageParams::includeContext),
        field("temperature", &CreateMessageParams::temperature),
        field("maxTokens", &CreateMessageParams::maxTokens),
        omitEmpty("stopSequences", &CreateMessageParams::stopSequences),
        omitEmpty("metadata", &CreateMessageParams::metadata));
}

constexpr auto jsonSchema(const CreateMessageResult *)
{
    return std::make_tuple(
        field("role", &CreateMessageResult::role),
        field("content", &CreateMessageResult::content),
        omitEmpty("model", &CreateMessageResult::model),
        omitEmpty("stopReason", &CreateMessageResult::stopReason));
}

constexpr auto jsonSchema(const ElicitRequestParams *)
{
    return std::make_tuple(
        field("message", &ElicitRequestParams::message),
        omitEmpty("requestedSchema", &ElicitRequestParams::requestedSchema),
        omitEmpty("mode", &ElicitRequestParams::mode),
        omitEmpty("url", &ElicitRequestParams::url));
}

constexpr auto jsonSchema(const ElicitResult *)
{
    return std::make_tuple(
        field("action", &ElicitResult::action), field("content", &ElicitResult::content));
}

} // namespace LLMQore::Mcp
