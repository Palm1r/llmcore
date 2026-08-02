# Thread contract

Every LLMQore object — `BaseClient` and its subclasses, `ToolsManager`, `ToolRegistry`,
`ToolHandler`, `McpClient`, `McpServer`, `AcpClient` — lives on the thread of the `QObject`
parent passed to its constructor.

- **All public methods must be called from that thread.**
- **All signals are emitted on that thread.**

Debug builds enforce the first rule with `Q_ASSERT_X` on every mutating or
raw-pointer-returning method. A release build will not warn you; it will race.

## Consuming from another thread

Connect with `Qt::AutoConnection` (the default). Qt queues the delivery and copies the
arguments, so a widget or model living on the GUI thread can observe a client running on a
worker thread without extra locking.

```cpp
connect(client, &LLMQore::BaseClient::chunkReceived,
        this, [this](const LLMQore::RequestID &, const QString &chunk) {
    m_view->append(chunk);
});
```

Everything crossing a signal is a value type — `Conversation`, `TurnContent`, `ToolResult`,
`CompletionInfo`, `ModelInfo`. Copying one is safe from any thread; none of them hand out a
pointer into library-owned storage.

## The one pointer rule

`ToolRegistry::registeredTools()` returns `QList<BaseTool *>` — raw pointers owned by the
registry. They are valid only until the next event-loop iteration, because removing a tool
deletes it.

For anything that outlives the immediate call — a UI list, a snapshot passed to another
thread — use `toolsSnapshot()`, which returns detached `ToolSnapshot` values:

```cpp
for (const auto &snapshot : client->tools()->toolsSnapshot())
    ui->addRow(snapshot.displayName, snapshot.description);
```

## Content blocks are values

Before 0.8.0, `BaseMessage` handed out `ContentBlock *` into a list that reallocated on
every append, so holding one across a parse step was a use-after-free waiting to happen.
Content is now a `std::variant` stored by value: `getCurrentBlocks()` returns
`const QList<TurnContent> &`, and `getCurrentToolUseContent()` /
`getCurrentThinkingContent()` return copies. There is no pointer to outlive.
