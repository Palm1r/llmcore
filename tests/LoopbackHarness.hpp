// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/JsonRpcSession.hpp>
#include <LLMQore/McpClient.hpp>
#include <LLMQore/McpServer.hpp>
#include <LLMQore/RpcPipeTransport.hpp>

#include "TestHelpers.hpp"

// The five-line dance a loopback test opens with -- create the pipe pair, put a
// peer on each end, start, and delete all four in the right order -- said once.

namespace LLMQoreTest {

// A raw JSON-RPC session on each end of a pipe: the shape a session-level test
// needs, with no protocol on top.
struct SessionPair
{
    LLMQore::Rpc::PipeTransport *localTransport = nullptr;
    LLMQore::Rpc::PipeTransport *remoteTransport = nullptr;
    LLMQore::Rpc::JsonRpcSession *local = nullptr;
    LLMQore::Rpc::JsonRpcSession *remote = nullptr;

    SessionPair()
    {
        auto [a, b] = LLMQore::Rpc::PipeTransport::createPair();
        localTransport = a;
        remoteTransport = b;
        local = new LLMQore::Rpc::JsonRpcSession(localTransport);
        remote = new LLMQore::Rpc::JsonRpcSession(remoteTransport);
        localTransport->start();
        remoteTransport->start();
    }

    ~SessionPair()
    {
        delete local;
        delete remote;
        delete localTransport;
        delete remoteTransport;
    }

    SessionPair(const SessionPair &) = delete;
    SessionPair &operator=(const SessionPair &) = delete;
};

// An McpServer and an McpClient joined by a pipe, started and ready to
// initialize.
struct McpPair
{
    LLMQore::Rpc::PipeTransport *serverTransport = nullptr;
    LLMQore::Rpc::PipeTransport *clientTransport = nullptr;
    LLMQore::Mcp::McpServer *server = nullptr;
    LLMQore::Mcp::McpClient *client = nullptr;

    explicit McpPair(const QString &serverName = QStringLiteral("test-server"))
    {
        auto [st, ct] = LLMQore::Rpc::PipeTransport::createPair();
        serverTransport = st;
        clientTransport = ct;
        server = new LLMQore::Mcp::McpServer(
            serverTransport, LLMQore::Mcp::McpServerConfig{{serverName, "0.0.1"}});
        client = new LLMQore::Mcp::McpClient(clientTransport);
        server->start();
    }

    ~McpPair()
    {
        delete client;
        delete server;
        delete serverTransport;
        delete clientTransport;
    }

    McpPair(const McpPair &) = delete;
    McpPair &operator=(const McpPair &) = delete;

    LLMQore::Mcp::InitializeResult initialize()
    {
        return waitForFuture(client->connectAndInitialize());
    }
};

} // namespace LLMQoreTest
