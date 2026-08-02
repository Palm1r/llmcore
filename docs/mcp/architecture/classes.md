# MCP class diagram

```mermaid
classDiagram
    class RpcTransport["Rpc::Transport"] {
        <<abstract>>
        // transport lifecycle + message I/O
    }

    class RpcPipeTransport["Rpc::PipeTransport"]
    class RpcStdioClientTransport["Rpc::StdioClientTransport"]
    class McpStdioServerTransport
    class McpHttpTransport
    class McpHttpServerTransport

    RpcTransport <|-- RpcPipeTransport
    RpcTransport <|-- RpcStdioClientTransport
    RpcTransport <|-- McpStdioServerTransport
    RpcTransport <|-- McpHttpTransport
    RpcTransport <|-- McpHttpServerTransport

    class JsonRpcSession["Rpc::JsonRpcSession"] {
        // request dispatch + handler registration
        // cancellation + progress reporting
    }

    JsonRpcSession --> RpcTransport : uses

    class McpClient {
        // handshake + capability negotiation
        // tool, resource, prompt operations
        // provider registration (roots, sampling, elicitation)
        toolsChanged()
        initialized(InitializeResult)
    }

    class McpServer {
        // tool, resource, prompt registration
        // logging + sampling + elicitation
    }

    McpClient --> JsonRpcSession : owns
    McpServer --> JsonRpcSession : owns
    McpClient --> BaseRootsProvider : optional
    McpClient --> BaseClient : optional (sampling)
    McpClient --> BaseElicitationProvider : optional
    McpServer --> BasePromptProvider : 0..*
    McpServer --> BaseResourceProvider : 0..*
    McpServer --> ToolRegistry : optional

    class BaseTool {
        <<abstract>>
        // identity + schema + async execution
    }

    class McpRemoteTool {
        // delegates execution to McpClient
        // id prefixed with server name
    }

    BaseTool <|-- McpRemoteTool
    McpRemoteTool --> McpClient : uses

    class McpToolBinder {
        // connect, list, wrap, diff-resync
        // reconnect with backoff for owned servers
        addServer(ServerEndpoint) bool
        loadServers(QJsonObject) int
        addClient(McpClient*, serverName, autoReconnect)
        removeClient(McpClient*)
        serverInitialized(name, InitializeResult)
        serverInitFailed(name, error)
        toolsSynced(name, toolCount)
        serverDisconnected(name)
    }

    McpToolBinder --> McpClient : owns or observes
    McpToolBinder --> ToolRegistry : registers McpRemoteTool into

    class BasePromptProvider {
        <<abstract>>
        // list, get, complete prompts
        listChanged()
    }

    class BaseResourceProvider {
        <<abstract>>
        // list, read, complete resources
        listChanged()
        resourceUpdated(uri)
    }

    class BaseRootsProvider {
        <<abstract>>
        // list workspace roots
        listChanged()
    }

    class BaseElicitationProvider {
        <<abstract>>
        // collect structured input from user
    }
```

## Ownership rules

- **`Rpc::JsonRpcSession`** — owned by `McpClient` or `McpServer`. Never outlives owner.
- **`Rpc::Transport`** — passed via constructor, NOT reparented. Caller owns lifetime.
- **`McpRemoteTool`** — parented to `ToolRegistry` (via `addTool`). Dies with registry or on `McpToolBinder` resync. Its id is `<server>_<tool>` when the binder knows the server name, so two servers exposing the same tool never collide.
- **Providers** (`BasePromptProvider`, `BaseResourceProvider`, `BaseRootsProvider`, `BaseElicitationProvider`) and sampling `BaseClient` — held as `QPointer`. Caller owns, must outlive server/client.
