# MCP exception hierarchy

All MCP errors propagate as `Rpc::JsonRpcException` subclasses. Inherits `QException` — flows through `QFuture`/`.onFailed()` with full type preservation.

```mermaid
classDiagram
    class QException {
        <<Qt base>>
    }

    class JsonRpcException["Rpc::JsonRpcException"] {
        // error message
    }

    class TransportError["Rpc::TransportError"]
    class ProtocolError["Rpc::ProtocolError"]
    class TimeoutError["Rpc::TimeoutError"]
    class CancelledError["Rpc::CancelledError"]
    class RemoteError["Rpc::RemoteError"] {
        // numeric code + remote message + data payload
    }

    QException <|-- JsonRpcException
    JsonRpcException <|-- TransportError
    JsonRpcException <|-- ProtocolError
    JsonRpcException <|-- TimeoutError
    JsonRpcException <|-- CancelledError
    JsonRpcException <|-- RemoteError
```

## Meanings

| Exception | When |
|---|---|
| `Rpc::TransportError` | Transport not open, network down, subprocess died, SSE stream broke |
| `Rpc::ProtocolError` | Invalid JSON-RPC envelope, unknown method, invalid state |
| `Rpc::TimeoutError` | Request exceeded `sendRequest` timeout |
| `Rpc::CancelledError` | Peer sent `notifications/cancelled`, or `cancelRequest` called |
| `Rpc::RemoteError` | Peer replied with JSON-RPC `error` object. Carries numeric `code`, remote `message`, `data` payload. Typically the one to catch separately in user code |

Every subtype implements `raise()` + `clone()` correctly — `.onFailed(ctx, [](const Rpc::RemoteError &e) {...})` matches the concrete subtype without slicing.
