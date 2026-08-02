# LLMQore

[![Build and Test](https://github.com/Palm1r/llmqore/actions/workflows/build_and_test.yml/badge.svg?branch=main)](https://github.com/Palm1r/llmqore/actions/workflows/build_and_test.yml)
![GitHub Tag](https://img.shields.io/github/v/tag/Palm1r/llmqore)

Qt/C++ library for putting an LLM inside a desktop application. Answers arrive as Qt
signals on your own thread, async work is `QFuture`, lifetimes are `QObject` parent-child.
Links against `Core`, `Network` and `Concurrent` — no GUI dependency.

## What you can do with it

- **Stream an answer into your UI** — tokens land in a widget or a model as they are generated
- **Let the model call your C++ code** — subclass `BaseTool`, the client runs the whole loop
- **Borrow tools from MCP servers** — filesystem, git, databases, anything with an MCP server
- **Expose your own tools outward** — an external agent calls into your running application
- **Run without the cloud** — Ollama and llama.cpp through the same code as Claude and OpenAI
- **Keep one conversation across providers** — no provider-specific JSON in your code
- **Drive an external coding agent** — launch Claude Code, render its output, serve its callbacks

Each of these is one block below, and one document deeper.

## Stream an answer into your UI

Ask and await the answer:

```cpp
auto *client = new LLMQore::ClaudeClient(
    "https://api.anthropic.com", apiKey, "claude-sonnet-4-5", this);

client->askOnce("What is Qt?").then(this, [this](const LLMQore::CompletionInfo &result) {
    m_view->setPlainText(result.fullText);
});
```

Or watch it arrive, which is what you want in a chat panel:

```cpp
connect(client, &LLMQore::BaseClient::accumulatedReceived,
        this, [this](const LLMQore::RequestID &, const QString &answer) {
    m_view->setPlainText(answer);
});

client->ask("What is Qt?");
```

`accumulatedReceived` carries the whole answer so far, so there is nothing to glue together;
`chunkReceived` carries just the new piece when appending is cheaper than repainting. Both
are emitted on the client's own thread — an ordinary `connect`, no worker thread, no
marshalling, no hand-parsed SSE.

→ [Quick Start](docs/quick-start.md)

## Let the model call your C++ code

Give the model a function over data only your application has — the open document, the
current selection, your database:

```cpp
client->tools()->addTool(new SearchCurrentFileTool(client));

conversation.addUser("Where do we handle the timeout?");
client->ask(conversation);
```

The client runs the loop: the model asks for the tool, your code executes, the result goes
back, the model answers. Bounded at ten rounds per request. `toolStarted` and
`toolResultReady` let you show it happening; `setExecutionGate()` lets you require
confirmation first.

→ [Quick Start](docs/quick-start.md) · [LLM clients](docs/llm-clients.md)

## Borrow tools from MCP servers

Tools from an MCP server join the same set and become indistinguishable to the model:

```cpp
client->tools()->addMcpServer({.name = "filesystem", .command = "npx",
    .arguments = {"-y", "@modelcontextprotocol/server-filesystem", "/home/user"}});

client->tools()->loadMcpServers(QJsonDocument::fromJson(configData).object());
```

`loadMcpServers` takes the config format Claude Desktop uses, so an existing
`mcpServers` block works unchanged:

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/home/user"]
    }
  }
}
```

→ [MCP coverage](docs/mcp/mcp_protocol_coverage.md)

## Expose your own tools outward

Hand that same registry to an MCP server and your application becomes one. Claude Code in a
terminal, Claude Desktop, or an editor can now call into your running application — while
your own model keeps calling the same tools:

```cpp
auto *server = new LLMQore::Mcp::McpServer(
    new LLMQore::Mcp::McpHttpServerTransport({.port = 8080, .path = "/mcp"}, this),
    cfg, this);

server->setToolRegistry(client->tools());
server->start();
```

Registered once, served both ways. Tools added later appear by themselves — the server
forwards `toolsChanged` as `notifications/tools/list_changed`.

Use HTTP for a server inside a running application; stdio takes over the process's standard
streams and belongs to programs that are nothing but an MCP server.

→ [Quick Start](docs/quick-start.md)

## Run without the cloud

Ollama and llama.cpp are ordinary clients — the same conversation, the same tools, the same
signals, no API key and nothing leaving the machine:

```cpp
auto *client = new LLMQore::OllamaClient("http://localhost:11434", {}, "llama3", this);
```

Switching between a local and a hosted model is a different constructor and nothing else.

→ [Supported providers](#supported-providers)

## Keep one conversation across providers

```cpp
LLMQore::Conversation conversation;
conversation.setSystem("Answer in one sentence.");
conversation.addUser("What is Qt?");

client->ask(conversation);
```

`messages` against `contents`, `assistant` against `model`, a top-level `system` field
against a system message inside the array — the client translates. The full history,
including everything the model added during tool rounds, comes back in
`CompletionInfo::conversation`, and replays against a different provider unchanged.

Turns are lists of content, so an image is just another block:

```cpp
conversation.addUser({
    LLMQore::TextContent{"What does this chart show?"},
    LLMQore::ImageContent::fromBytes(png, "image/png")});
```

→ [LLM clients](docs/llm-clients.md)

## Drive an external coding agent

The other direction: instead of giving your application a model, give it somebody else's
agent. LLMQore launches Claude Code or Codex over stdio, streams its output as Qt signals,
and serves its callbacks for permission, file system and terminal. The agent owns the model
and the tool loop; you render and approve.

```cpp
using namespace LLMQore::Acp;

AcpAgentRegistry registry;
registry.loadFromFile("agents.json");

auto *agent = new AcpClient(
    registry.config("claude", QDir::currentPath())->createTransport(this), {}, this);
agent->setFileSystemProvider(new DefaultFileSystemProvider(this));
agent->setTerminalProvider(new TerminalManager(this));

connect(agent, &AcpClient::agentMessageChunk,
        this, [](const QString &, const ContentBlock &c) { /* render c.text */ });

agent->connectAndInitialize();   // then newSession() -> prompt()
```

Agents are data, not code — a JSON catalogue, overridable with `LLMQORE_ACP_AGENTS`.
Credentials never travel through the protocol; the agent authenticates itself.

→ [ACP host](docs/acp/architecture.md) · [authentication](docs/acp/authentication.md)

## MCP Bridge

A standalone CLI built on the library: aggregates several MCP servers and re-exposes them
behind one HTTP/SSE endpoint or one stdio server, for when the upstreams and the client
disagree on transport.

```bash
mcp-bridge bridge.json              # HTTP endpoint
mcp-bridge --stdio bridge.json      # stdio
```

Prebuilt binaries with the Qt runtime bundled are on
[Releases](https://github.com/Palm1r/llmqore/releases).

→ [MCP Bridge](docs/mcp-bridge.md)

## All of it together

<img width="912" alt="example-chat" src="https://github.com/user-attachments/assets/2fb1ea83-1d2d-4016-9c87-56180dbf3301" />

[`example-chat`](example/Main.qml) is a working Qt Quick application with provider
switching, tool badges, MCP servers and a real coding agent over ACP. Build it with
`-DLLMQORE_BUILD_EXAMPLES=ON`.

## Supported providers

| Provider | Client class | Streaming | Tools | Images in | Reasoning parsed | Reasoning replayed |
|---|---|---|---|---|---|---|
| Anthropic Claude | `ClaudeClient` | ✓ | ✓ | ✓ | ✓ | ✓ signature |
| OpenAI (Chat Completions) | `OpenAIClient` | ✓ | ✓ | ✓ | ✓ | ✓ when received |
| OpenAI (Responses API) | `OpenAIResponsesClient` | ✓ | ✓ | ✓ | ✓ | opt-in |
| Google AI | `GoogleAIClient` | ✓ | ✓ | ✓ | ✓ | ✓ thought signature |
| Ollama | `OllamaClient` | ✓ | ✓ | ✓ | ✓ | ✓ |
| Mistral | `MistralClient` | ✓ | ✓ | ✓ | ✓ | ✓ when received |
| llama.cpp | `LlamaCppClient` | ✓ | ✓ | ✓ | ✓ | ✓ when received |
| DeepSeek | `OpenAIClient` | ✓ | ✓ | ✓ | ✓ | ✓ when received |

**Reasoning parsed** means thinking blocks reach you as signals. **Reasoning replayed**
means they go back into the next request in the form that provider requires — without which
some models reject a continuation that follows a tool call. The Responses API needs
`store: false` to make this work, so it is a switch rather than a default; see
[LLM clients](docs/llm-clients.md).

MCP is implemented for the 2025-11-25 spec over stdio and Streamable HTTP — server side:
tools, resources, resource templates, prompts, completions, sampling, elicitation; client
side: the same plus roots.

## Requirements

- C++20
- Qt 5.15 or Qt 6.5+
- CMake 3.21+

CI builds and tests Qt 6.8.3 and 6.10.2 on Linux, macOS and Windows, and Qt 5.15.2 on
Linux. Versions inside the stated range but outside that matrix are expected to work and
are not verified on every commit. The Qt Quick example is Qt 6 only.

## Documentation

- [Quick Start](docs/quick-start.md) — a complete program, from CMake to QML
- [LLM clients](docs/llm-clients.md) — conversations, models, tools, headers, escape hatches
- [Threading](docs/threading.md) — the thread contract, in full
- [Integration](docs/integration.md) — FetchContent, installed builds, CMake options
- [MCP Bridge](docs/mcp-bridge.md) — aggregate MCP servers behind one endpoint
- [MCP coverage](docs/mcp/mcp_protocol_coverage.md) — spec conformance matrix
- [ACP host](docs/acp/architecture.md) — drive external agents ([as-built notes](docs/acp/implementation-notes.md))
- [Architecture](docs/architecture.md) — internals, for contributors

## Support

- **Report Issues**: [open an issue](https://github.com/Palm1r/llmqore/issues) on GitHub
- **Contribute**: pull requests with bug fixes or new features are welcome
- **Spread the Word**: star the repository and share with fellow developers
- **Financial Support**:
   - Paypal: [paypal page](https://www.paypal.com/paypalme/palm1r)
   - Bitcoin (BTC): `bc1qndq7f0mpnlya48vk7kugvyqj5w89xrg4wzg68t`
   - Ethereum (ETH): `0xA5e8c37c94b24e25F9f1f292a01AF55F03099D8D`
   - Litecoin (LTC): `ltc1qlrxnk30s2pcjchzx4qrxvdjt5gzuervy5mv0vy`
   - USDT (TRC20): `THdZrE7d6epW6ry98GA3MLXRjha1DjKtUx`

## License

MIT — see [LICENSE](LICENSE).
