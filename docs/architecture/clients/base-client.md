# BaseClient contract

Abstract base for every LLM provider client. Owns HTTP transport, request bookkeeping, the tool-call loop, and signal dispatch. Subclasses supply the wire format, message model, and continuation shape.

---

## Responsibilities

**HTTP transport.** `BaseClient` issues both buffered and streaming HTTP requests through an `HttpTransport`, and manages the lifecycle of each streaming reply, wiring chunk and completion signals into internal handlers. The transport is a constructor argument -- every provider client accepts one after `model` -- and defaults to a privately owned `HttpClient`; a supplied transport stays owned by the caller. Passing a fake transport is how provider clients are tested end to end (see `tests/FakeHttpTransport.hpp`): SSE bytes go in, `chunkReceived` / `requestCompleted` come out, and the continuation request of a tool round-trip can be inspected as it is sent.

**Request bookkeeping.** Every in-flight request is tracked in a central map keyed by a unique request ID. Each entry holds the stream handle, response buffers, the original URL and payload (needed for tool continuations), a continuation counter, and the captured stop reason. This map is the single source of truth for request state.

**Tool-call loop.** When a streamed response ends with pending tool calls, `BaseClient` walks the tool-use blocks from the message, dispatches them through `ToolsManager`, collects results, and asks the provider subclass to build a continuation payload. The new payload is re-posted under the same request ID. This loop is bounded to a fixed maximum number of continuations to prevent runaways.

**Request headers and authentication.** `BaseClient` builds every outgoing `QNetworkRequest` itself, from two pieces of state the caller can read and replace: a header map and an `AuthScheme`. Provider clients seed both in their constructor -- Claude with `x-api-key` plus `anthropic-version`, the OpenAI-shaped clients with `Authorization: Bearer`, Google with a `key` query parameter -- and the caller overrides whatever it needs. `setHeader` sets one entry, `setHeaders` replaces the whole map, `setAuthScheme` moves the key to a different header or query parameter. An empty API key sends no credential at all.

**Thinking blocks.** `notifyPendingThinkingBlocks` emits `thinkingBlockReceived` once per completed thinking block, never per delta: it tracks how many blocks a request has already published and emits only the new ones, resetting the count at each tool continuation. Providers accumulate reasoning into their message object and call it when the reasoning is complete -- when answer text starts arriving, and again at the finish reason. Claude emits directly instead, because its wire format has explicit `content_block_stop` boundaries.

**Signal dispatch.** Text deltas, thinking blocks, tool start/result events, final completion, and errors are delivered as Qt signals (`chunkReceived`, `accumulatedReceived`, `thinkingBlockReceived`, `toolStarted`, `toolResultReady`, `requestCompleted`, `requestFinalized`, `requestFailed`). All signals are emitted on the `BaseClient`'s owning thread; Qt's default `AutoConnection` queues cross-thread delivery safely.

---

## Provider subclass contract

Each provider must supply implementations for several categories of functionality:

- **Wire format**: declare which tool schema format the provider uses, and seed the default `AuthScheme` and header map in the constructor. Building the request is the base's job, not the subclass's.
- **Streaming parser**: accept raw bytes from the HTTP stream and push them through the appropriate framer (SSE or JSON-lines), updating the provider's message object as events arrive. A separate path handles buffered (non-streamed) responses.
- **Message bookkeeping**: maintain a per-request message object (a `BaseMessage` subclass) and clean it up when the request ends. The object is created lazily, on the first stream event that has something to record -- not in `sendMessage`, which may never see a response at all.
- **Continuation**: given the original payload, the current message state, and the collected tool results, build a new JSON payload that includes the assistant's response and the tool results in the provider's wire format.
- **Error parsing** (optional): extract a human-readable error message from provider-specific HTTP error response bodies.

### Typical sendMessage pattern

```cpp
RequestID FooClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    const RequestID id = createRequest();
    const QString resolved = endpoint.isEmpty() ? QStringLiteral("/chat") : endpoint;
    sendRequest(id, QUrl(m_url + resolved), payload, mode);
    return id;
}
```

The `FooMessage` for `id` is allocated later, by the stream handler, on the
first chunk that carries content -- see the lazy `m_messages.value(id)` /
`new FooMessage` pattern in every provider's stream-processing method.

`endpoint` lets the caller pick a non-default path on providers that
expose more than one (e.g. Mistral's `/fim/completions`). Passing an
empty string selects the provider default, so single-endpoint clients
can ignore the argument beyond the empty-check shown above.

Consumers subscribe to `BaseClient` signals (`chunkReceived`,
`requestCompleted`, `requestFinalized`, `requestFailed`, `toolStarted`,
`toolResultReady`, `thinkingBlockReceived`) to observe request progress.
All signals are emitted on the `BaseClient`'s owning thread; Qt's
`AutoConnection` handles cross-thread queued delivery.

---

## Error handling

Errors reach the caller through three paths:

- **Transport errors** -- DNS failures, timeouts, SSL errors, aborted connections, and connection refused. These originate from the network layer and are forwarded as failure notifications.
- **HTTP errors (4xx/5xx)** -- When non-success status headers arrive, the client switches to error mode and accumulates the response body. At stream end, the provider gets a chance to parse the vendor-specific error format. If it declines, a default message with the status code and a body snippet is used.
- **Parse/protocol errors** -- Malformed streams or unexpected JSON structures detected during provider-specific parsing. The provider reports these directly as request failures.

---

## Checklist: adding a new provider

1. Create a new folder under `source/clients/` with the client and message implementation files.
2. Add a public header under `include/LLMQore/`.
3. Add a tool schema format enumerator if the provider's tool definition shape differs from existing ones, and teach `ToolsManager` to produce it.
4. Implement the provider subclass contract: message sending, model listing, stream/buffered parsing, message bookkeeping, continuation building, and optionally error parsing. Set the default `AuthScheme` and headers in the constructor -- there is no request-building hook to override.
5. Add unit tests covering constructor sanity, payload shape, and any provider-specific quirks.
