# gipMultiplayer

A multiplayer networking plugin for GlistEngine. It exposes the [znet](https://github.com/teoncreative/znet) networking library as `gipMultiplayer` (a namespace alias to `znet`) and adds server-authoritative team voice on top of it.

## Setup

### Requirements

- CMake 3.29 or newer, required by znet.
- OpenSSL. Bundled with the glist toolchain on Windows, install it from your package manager on Linux and macOS.
- Network access on the first configure, when znet, zstd, gipOpus and libopus are downloaded.

### 1. Clone into your `glistplugins` directory

```bash
cd path/to/your/glistplugins
git clone https://github.com/GlistPlugins/gipMultiplayer.git
```

### 2. Add to your project's CMakeLists.txt

```cmake
set(PLUGINS gipMultiplayer)
```

znet and [gipOpus](https://github.com/GlistPlugins/gipOpus) are fetched at configure time, pinned to the commits in [`external/znet.cmake`](external/znet.cmake) and [`external/gipopus.cmake`](external/gipopus.cmake). Their own dependencies, zstd and libopus, come with them. gipOpus lands in `glistplugins/gipOpus`, znet and zstd stay in the app's build tree.

To build against a local znet checkout instead of the pinned commit:

```bash
cmake -DFETCHCONTENT_SOURCE_DIR_ZNET=/path/to/znet ...
```

### 3. Include in your code

```cpp
#include "gipMultiplayer.h"
```

## Local Voice Loopback

`gVoiceLoopback` checks the local microphone pipeline with either delayed raw PCM or an Opus encode/decode round trip. It uses the default capture and playback devices and sends nothing over the network.

```cpp
#include "audio/gVoiceLoopback.h"
```

In the [`GlistApp-TeamVoice`](examples/GlistApp-TeamVoice) example, `[3]` starts the Opus loopback, `Space` stops or restarts it and `Esc` returns to the menu. On Windows, configure, build and launch it from the repository root with:

```powershell
& .\examples\GlistApp-TeamVoice\run-team-voice.ps1
```

Use headphones, otherwise the microphone picks up the playback and feeds back.

## Network Team Voice

`gTeamVoice` does Opus capture, per-speaker jitter buffering and mixed playback on the client. `gTeamVoiceServer` relays each uplink only to the peers the server has authorized for the same team. Clients cannot pick their own sender identity, team or recipients.

```cpp
gTeamVoice voice;

// Call after GlistEngine's sound system is available.
if (!voice.initialize()) {
	gLoge("Voice initialization failed: " + voice.getLastError());
}

// Forward voice packets from the znet handler, then pump outgoing audio from
// the same update path that owns the connected session.
voice.handleSessionPacket(*sessionPacket);
voice.handleVoicePacket(*downlinkPacket);
voice.updateNetwork(*peerSession);

// Push-to-talk input.
voice.startTransmitting();
voice.stopTransmitting();
```

Register the three voice serializers with the same `znet::Codec` the game uses. On the server, call `gTeamVoiceServer::setPeerState()` from authenticated game state whenever a player joins a match, changes team or changes voice permissions, and `removePeer()` on disconnect.

[TUTORIAL.md](TUTORIAL.md) walks through a first run. [TEAM_VOICE.md](TEAM_VOICE.md) has the full server and client handlers, lifecycle rules, threading and diagnostics.

### Two-Computer Voice Test

Run `run-team-voice.ps1` on both Windows computers. On the host choose `[1]`, keep bind IP `0.0.0.0` and port `25000`, and allow the app through Windows Firewall on private networks; ZDT runs over UDP. On the other computer choose `[2]` and enter the host's LAN IPv4 address with the same port.

Once the HUD shows `Voice: LISTENING`, hold `V` on the speaking computer only. The other side plays the audio without pressing anything. Releasing `V` stops that computer's uplink but leaves it listening. `Esc` returns to the menu and shuts the sessions down cleanly.

The example puts every ready connection on server-owned team `1`. That is fine for a test but it is not authentication, a real game must call `setPeerState()` from its own player, match, team and permission model.

## Replication Example

[`GlistApp-Multiplayer`](examples/GlistApp-Multiplayer) uses an abstract `GameBackend` with node replication. The canvas doesn't care how the connection works, it just attaches `gNode` objects and they get synced automatically.

### Architecture

```
src/
  net/
    GamePackets.h           - Packet definitions (NodeState, NodeLeave) and serializers
    GameBackend.h/cpp       - Abstract base class for node replication
    GameBackendLocal.h/cpp  - Host backend (runs server, broadcasts to clients)
    GameBackendRemote.h/cpp - Client backend (connects to server)
  canvas/
    MenuCanvas.h/cpp        - Mode selection (Host/Client), IP and port input
    GameCanvas.h/cpp        - Game rendering, input handling, remote node management
    AudioCanvas.h/cpp       - Microphone and Opus loopback diagnostics
  gApp.h/cpp                - Application entry point
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

### Running It

Start two instances of the example app. In the first choose Host, which binds to `0.0.0.0`. In the second choose Client and connect to `127.0.0.1`, or the host's LAN IP for a cross-machine test. Both default to port `25000`.

## Testing

The voice suite is standalone C++17 and does not need a GlistEngine application:

```bash
cmake -S tests -B build/voice-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/voice-tests
ctest --test-dir build/voice-tests --output-on-failure
```

It covers packet bounds, authoritative routing and permissions, rate limits, Opus encode/decode, jitter, reordering, duplicate and late packets, loss and PLC, queue overflow, lifecycle barriers, stream restart, and an encrypted four-client ZDT team isolation test.
