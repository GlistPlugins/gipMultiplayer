# Threading Model

This example runs on two threads: the main thread (GlistEngine's render loop) and znet's network threads. Knowing which code runs where is what keeps the shared state from racing.

## The Two Threads

### Main Thread (Render Loop)

GlistEngine calls `update()` and `draw()` on the main thread every frame. All game logic, rendering and node manipulation happens here.

Runs:
- `GameCanvas::update()`, moves the local player, calls `backend->update()`
- `GameCanvas::draw()`, renders all boxes
- `GameBackend::update()`, drains the event queue, applies positions to remote nodes, sends local node positions

### Network Thread(s) (znet)

znet runs a pool of background threads for network I/O and distributes sessions across them. When a packet arrives or a connection event happens, znet calls our handlers on one of those threads, **not the main thread**. Two `ServerPacketHandler::OnPacket()` calls for different clients can therefore run at the same time on different threads, which is why the locks below protect the sessions list and the event queue.

Runs:
- `ServerPacketHandler::OnPacket()`, when a client sends data to the host
- `ClientPacketHandler::OnPacket()`, when the server sends data to a client
- `GameBackendLocal::onPeerConnected/Disconnected()`, when a client connects or disconnects
- `GameBackendRemote::onConnected/Disconnected()`, when we connect to or disconnect from a server

## Why We Need Locks

The network thread writes data that the main thread reads. Without synchronization both threads could touch the same data at once, causing crashes or corrupted state.

### queuemutex (in GameBackend)

Protects the event queue (`std::vector<QueuedEvent>`).

- **Network thread writes:** `enqueueState()` and `enqueueLeave()` push events onto the queue when packets arrive.
- **Main thread reads:** `update()` swaps the entire queue into a local variable, then processes it.

The swap pattern (`batch.swap(queue)`) holds the lock only for the swap, not for the whole processing loop, so the network thread is barely blocked.

```
Network thread:                    Main thread:
enqueueState() {                   update() {
  lock(queuemutex)                   lock(queuemutex)
  queue.push_back(...)               batch.swap(queue)  // instant
  unlock                             unlock
}                                    // process batch without holding lock
                                   }
```

### sessionsmutex (in GameBackendLocal)

Protects the sessions list (`std::vector<shared_ptr<PeerSession>>`).

- **Network thread writes:** `onPeerConnected()` adds sessions, `onPeerDisconnected()` removes them.
- **Main thread reads (indirectly):** `broadcastState()` iterates the list to send packets, called from `update()`.
- **Network thread also reads:** `broadcast()` in `ServerPacketHandler::OnPacket()` iterates the list to forward packets to other clients.

Multiple threads can read and write the sessions list at the same time, so every access is guarded.

## What Doesn't Need Locks

- `localbox`, `remoteboxes` and key state, only accessed on the main thread (in `update()` and `draw()`).
- `GameBackend::nodes`, only accessed in `update()`, which runs on the main thread. `attachNode/detachNode` are also called from the main thread, in `setup()` and in callbacks within `update()`.
- `session` in `GameBackendRemote`, set once in `onConnected()` (network thread) and read in `broadcastState()` (main thread). This is technically a race, but in practice the session is set before the first `update()` call and only cleared on disconnect.

## Data Flow Diagram

![Data Flow](./dataflow.svg)
