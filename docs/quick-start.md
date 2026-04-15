# Quick Start

## LLM Clients

### Minimal example

```cpp
#include <LLMQore/Clients>

auto *client = new LLMQore::ClaudeClient(
    "https://api.anthropic.com", "sk-...", "claude-sonnet-4-20250514", this);

LLMQore::RequestCallbacks cb;
cb.onChunk = [](const LLMQore::RequestID &, const QString &chunk) {
    qDebug() << chunk;
};
cb.onCompleted = [](const LLMQore::RequestID &, const QString &full) {
    qDebug() << "Done:" << full;
};
cb.onFailed = [](const LLMQore::RequestID &, const QString &err) {
    qWarning() << "Error:" << err;
};

client->ask("What is Qt?", cb);
```

### Full payload control

```cpp
QJsonObject payload;
payload["model"] = "claude-sonnet-4-20250514";
payload["max_tokens"] = 4096;
payload["stream"] = true;
payload["messages"] = QJsonArray{
    QJsonObject{{"role", "user"}, {"content", "Explain RAII in C++"}}
};

client->sendMessage(payload, cb);
```

### Using signals instead of callbacks

```cpp
auto *client = new LLMQore::OpenAIClient(url, apiKey, model, this);

connect(client, &LLMQore::BaseClient::chunkReceived,
        this, [](const LLMQore::RequestID &, const QString &chunk) {
    qDebug() << chunk;
});

connect(client, &LLMQore::BaseClient::requestCompleted,
        this, [](const LLMQore::RequestID &, const QString &full) {
    qDebug() << "Done:" << full;
});

client->ask("Hello!");
```

### Thinking / reasoning blocks

```cpp
LLMQore::RequestCallbacks cb;
cb.onThinkingBlock = [](const LLMQore::RequestID &,
                         const QString &thinking,
                         const QString &signature) {
    qDebug() << "Thinking:" << thinking.left(200) << "...";
};
```

### Cancel a request

```cpp
LLMQore::RequestID id = client->ask("Write a long essay...", cb);
// ...later:
client->cancelRequest(id);
```

## Tools

### Define a tool

Subclass `BaseTool` to create a tool that LLM can call:

```cpp
#include <LLMQore/BaseTool.hpp>
#include <QtConcurrent>

class GetWeatherTool : public LLMQore::BaseTool
{
    Q_OBJECT
public:
    using BaseTool::BaseTool;

    QString id() const override { return "get_weather"; }
    QString displayName() const override { return "Get Weather"; }
    QString description() const override { return "Returns current weather for a city."; }

    QJsonObject parametersSchema() const override
    {
        return QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"city", QJsonObject{{"type", "string"}, {"description", "City name"}}},
            }},
            {"required", QJsonArray{"city"}}
        };
    }

    QFuture<LLMQore::ToolResult> executeAsync(const QJsonObject &input) override
    {
        return QtConcurrent::run([input]() -> LLMQore::ToolResult {
            QString city = input["city"].toString();
            // ... fetch real weather data ...
            return LLMQore::ToolResult::text(QString("22°C, sunny in %1").arg(city));
        });
    }
};
```

### Register and use with an LLM client

```cpp
client->tools()->addTool(new GetWeatherTool(client));
client->ask("What's the weather in Berlin?", cb);
```

The tool works the same way whether registered directly or exposed through an MCP server.

### Register in an MCP server

```cpp
server->addTool(new GetWeatherTool(server));
```

Now any MCP client connecting to this server will see and can call `get_weather`.

## MCP Server

### stdio transport

For tools that integrate with Claude Desktop, Cursor, VS Code, etc.:

```cpp
#include <LLMQore/Mcp>

auto *transport = new LLMQore::McpStdioServerTransport(&app);

LLMQore::McpServerConfig cfg;
cfg.serverInfo = {"my-server", "1.0.0"};
cfg.instructions = "My MCP server with custom tools";

auto *server = new LLMQore::McpServer(transport, cfg, &app);
server->addTool(new MyCustomTool(server));
server->start();
```

### Streamable HTTP transport

For remote or multi-client access:

```cpp
#include <LLMQore/Mcp>

LLMQore::HttpServerConfig httpCfg;
httpCfg.port = 8080;
httpCfg.path = "/mcp";

auto *transport = new LLMQore::McpHttpServerTransport(httpCfg, &app);

LLMQore::McpServerConfig cfg;
cfg.serverInfo = {"my-server", "1.0.0"};

auto *server = new LLMQore::McpServer(transport, cfg, &app);
server->addTool(new MyCustomTool(server));
server->start();
```

## MCP Client

### Add servers programmatically

```cpp
// stdio — launch an MCP server as a subprocess
client->tools()->addMcpServer({
    .name = "filesystem",
    .command = "npx",
    .arguments = {"-y", "@modelcontextprotocol/server-filesystem", "/home/user"}
});

// Streamable HTTP — connect to a remote MCP server
client->tools()->addMcpServer({
    .name = "remote-tools",
    .url = QUrl("http://localhost:8080/mcp")
});
```

### Load from a JSON config

```cpp
QFile file("mcp_servers.json");
file.open(QIODevice::ReadOnly);
client->tools()->loadMcpServers(QJsonDocument::fromJson(file.readAll()).object());
```

Config format (compatible with Claude Desktop):

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/home/user"]
    },
    "database": {
      "command": "uvx",
      "args": ["mcp-server-sqlite", "--db-path", "/path/to/db.sqlite"]
    },
    "remote": {
      "url": "http://localhost:8080/mcp",
      "headers": {
        "Authorization": "Bearer token"
      }
    }
  }
}
```

### Share one MCP server across multiple LLM providers

```cpp
auto *mcpClient = new LLMQore::McpClient(transport, {"my-app", "1.0.0"}, &app);
mcpClient->connectAndInitialize();

claudeClient->tools()->addMcpClient(mcpClient);
openaiClient->tools()->addMcpClient(mcpClient);
```

### Use the MCP client directly

```cpp
auto *transport = new LLMQore::McpStdioClientTransport(
    {.program = "my-mcp-server", .arguments = {"--verbose"}}, &app);
auto *mcpClient = new LLMQore::McpClient(transport, {"my-app", "1.0.0"}, &app);

mcpClient->connectAndInitialize().then([mcpClient]() {
    // List available tools
    mcpClient->listTools().then([](QList<LLMQore::ToolInfo> tools) {
        for (const auto &tool : tools)
            qDebug() << tool.name << "-" << tool.description;
    });

    // Call a tool directly
    mcpClient->callTool("get_datetime", {}).then([](LLMQore::ToolResult r) {
        qDebug() << "Result:" << r.asText();
    });

    // List resources
    mcpClient->listResources().then([](QList<LLMQore::ResourceInfo> resources) {
        for (const auto &r : resources)
            qDebug() << r.uri << "-" << r.name;
    });
});
```
