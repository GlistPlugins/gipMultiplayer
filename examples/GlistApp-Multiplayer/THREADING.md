# Threading Model

This example runs on two threads: the **main thread** (GlistEngine's render loop) and the **network thread** (znet's internal thread). Understanding which code runs where is key to avoiding race conditions and a lot of headaches down the line. 

## The Two Threads

### Main Thread (Render Loop)
GlistEngine calls `update()` and `draw()` on the main thread every frame. All game logic, rendering, and node manipulation happens here.

Runs:
- `GameCanvas::update()` — moves the local player, calls `backend->update()`
- `GameCanvas::draw()` — renders all boxes
- `GameBackend::update()` — drains the event queue, applies positions to remote nodes, sends local node positions

### Network Thread(s) (znet)
znet runs a pool of background threads for network I/O, distributing sessions across them. When a packet arrives or a connection event happens, znet calls our handlers on one of these threads, **not the main thread**. This means two `ServerPacketHandler::OnPacket()` calls for different clients can run concurrently on different threads — which is why the locks protect shared state like the sessions list and event queue.

Runs:
- `ServerPacketHandler::OnPacket()` — when a client sends data to the host
- `ClientPacketHandler::OnPacket()` — when the server sends data to a client
- `GameBackendLocal::onPeerConnected/Disconnected()` — when a client connects/disconnects
- `GameBackendRemote::onConnected/Disconnected()` — when we connect to/disconnect from a server

## Why We Need Locks

The problem: the network thread writes data that the main thread reads. Without synchronization, both threads could access the same data at the same time, causing crashes or corrupted state.

### queuemutex (in GameBackend)

Protects the event queue (`std::vector<QueuedEvent>`).

- **Network thread writes:** `enqueueState()` and `enqueueLeave()` push events onto the queue when packets arrive.
- **Main thread reads:** `update()` swaps the entire queue into a local variable, then processes it.

The swap pattern (`batch.swap(queue)`) is important — it holds the lock for just the swap, not for the entire processing loop. This keeps the lock duration minimal so the network thread isn't blocked.

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

Multiple threads can be reading and writing the sessions list simultaneously, so every access is guarded.

## What Doesn't Need Locks

- `localbox`, `remoteboxes`, key state — only accessed on the main thread (in `update()` and `draw()`).
- `GameBackend::nodes` map — only accessed in `update()` which runs on the main thread. `attachNode/detachNode` are also called from the main thread (in `setup()` and callbacks within `update()`).
- `session` (in GameBackendRemote) — set once in `onConnected()` (network thread), read in `broadcastState()` (main thread). This is technically a race, but in practice the session is set before the first `update()` call and only cleared on disconnect.

## Data Flow Diagram

![Data Flow](./dataflow.svg)
