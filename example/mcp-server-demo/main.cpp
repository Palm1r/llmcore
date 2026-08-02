// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <QCoreApplication>

#include <LLMQore/Mcp>

#include "../ExampleTools.hpp"

using namespace LLMQore::Mcp;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    auto *transport = new McpStdioServerTransport(&app);

    McpServerConfig cfg;
    cfg.serverInfo = {"llmqore-host", "0.1.0"};
    cfg.instructions
        = "Example MCP server exposing host-inspection tools (IPv4, environment "
          "variables, image file reader) via llmqore.";

    auto *server = new McpServer(transport, cfg, &app);
    server->addTool(new Example::IPv4Tool(server));
    server->addTool(new Example::EnvTool(server));
    server->addTool(new Example::ImageReadTool(server));

    server->start();
    return app.exec();
}
