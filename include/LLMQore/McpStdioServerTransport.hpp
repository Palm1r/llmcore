// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include <LLMQore/LLMQore_global.h>
#include <LLMQore/RpcTransport.hpp>

class QIODevice;

namespace LLMQore::Mcp {

class LLMQORE_EXPORT McpStdioServerTransport : public Rpc::Transport
{
    Q_OBJECT
public:
    explicit McpStdioServerTransport(QObject *parent = nullptr);

    // Reads newline-framed JSON from `input` and writes replies to `output`
    // instead of the process-wide stdin/stdout. Both devices must be opened
    // by the caller and outlive the transport.
    McpStdioServerTransport(QIODevice *input, QIODevice *output, QObject *parent = nullptr);

    ~McpStdioServerTransport() override;

    void start() override;
    void stop() override;
    bool isOpen() const override;
    void send(const QJsonObject &message) override;

private:
    void readFromDevice();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LLMQore::Mcp
