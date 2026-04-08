# gipMultiplayer

A multiplayer networking plugin for GlistEngine. Provides the [znet](https://github.com/teoncreative/znet) networking library as `gipMultiplayer` (namespace alias to `znet`).

## Setup

### 1. Clone into your `glistplugins` directory

```bash
cd path/to/your/glistplugins
git clone https://github.com/GlistPlugins/gipMultiplayer.git
```

### 2. Initialize submodules (for znet)

```bash
cd gipMultiplayer
git submodule update --init --recursive
```

### 3. Add to your project's CMakeLists.txt

```cmake
set(PLUGINS gipMultiplayer)
```

### 4. Include in your code

```cpp
#include "gipMultiplayer.h"
```

## Example

The example uses an abstract `GameBackend` with node replication. The canvas doesn't care how the connection works, it just attaches `gNode` objects and they get synced automatically.

### Architecture

```
src/
  net/
    GamePackets.h         - Packet definitions (NodeState, NodeLeave) and serializers
    GameBackend.h/cpp     - Abstract base class for node replication
    GameBackendLocal.h/cpp - Host backend (runs server, broadcasts to clients)
    GameBackendRemote.h/cpp - Client backend (connects to server)
  canvas/
    MenuCanvas.h/cpp      - Mode selection (Host/Client), IP and port input
    GameCanvas.h/cpp      - Game rendering, input handling, remote node management
  gApp.h/cpp              - Application entry point
  main.cpp
```

### Key Concepts

**GameBackend** manages a registry of `gNode*` pointers tagged as local or remote:
- Local nodes: their position is sent to remote peers every frame.
- Remote nodes: their position is updated from incoming network data.
- Subclasses implement `broadcastState()` for the actual transport.

**GameBackendLocal** (host) runs a server. When a client sends its position, the server handler enqueues it into the backend and rebroadcasts to all other clients. The host's own local nodes are sent directly to every client.

**GameBackendRemote** (client) connects to a server. Sends local node positions to the server, receives other clients' positions via server broadcasts.

**GameCanvas** owns the visuals. It creates a `gBox` for the local player, registers `onJoin`/`onLeave` callbacks to create and destroy remote player visuals, and calls `backend->update()` each frame.

### Usage

```cpp
// Attach any gNode for network syncing
backend->attachNode(myId, &localBox, true);    // local - we send its position
backend->attachNode(remoteId, &remoteBox, false); // remote - we receive its position

// Callbacks for when remote nodes appear or disappear
backend->setOnJoin([](uint32_t id) { /* create visual, call attachNode */ });
backend->setOnLeave([](uint32_t id) { /* call detachNode, cleanup */ });

// Call once per frame - processes incoming events, sends local state
backend->update();
```

### Threading

Network events fire on znet's background thread pool, not the main thread. A thread-safe queue bridges the two: handlers enqueue events from the network thread, `update()` drains them on the main thread. See [THREADING.md](examples/GlistApp-Multiplayer/THREADING.md) for details and a data flow diagram.

## Testing

Run two instances of the example app. In the first, choose Host (binds to `0.0.0.0`). In the second, choose Client and connect to `127.0.0.1` (or the host's LAN IP for cross-machine testing). Both use port `25000` by default.
