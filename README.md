# gipMultiplayer

A multiplayer networking plugin for GlistEngine. Provides the [znet](https://github.com/teoncreative/znet) networking library as `gipMultiplayer` (namespace alias to `znet`).

## Setup

### Requirements

- **CMake 3.29 or newer** — required by znet.
- **OpenSSL** — bundled with the glist toolchain on Windows; install it from your package manager on Linux and macOS.
- **Network access on the first configure** — znet, zstd, gipOpus and libopus are downloaded then.

### 1. Clone into your `glistplugins` directory

```bash
cd path/to/your/glistplugins
git clone https://github.com/GlistPlugins/gipMultiplayer.git
```

### 2. Add to your project's CMakeLists.txt

```cmake
set(PLUGINS gipMultiplayer)
```

znet and [gipOpus](https://github.com/GlistPlugins/gipOpus) are fetched at configure time and pinned to known-good commits in [`external/znet.cmake`](external/znet.cmake) and [`external/gipopus.cmake`](external/gipopus.cmake). The nested zstd and libopus dependencies are fetched automatically too, so there is nothing else to install or check out. gipOpus is placed in `glistplugins/gipOpus`; znet and zstd remain in the app's build tree.

If you want to build against a local znet checkout instead of the pinned commit:

```bash
cmake -DFETCHCONTENT_SOURCE_DIR_ZNET=/path/to/znet ...
```

### 3. Include in your code

```cpp
#include "gipMultiplayer.h"
```

## Local Voice Loopback

`gVoiceLoopback` validates the local microphone pipeline with either delayed raw PCM or an Opus encode/decode round trip. It uses the default capture and playback devices and does not send voice over the network.

```cpp
#include "audio/gVoiceLoopback.h"
```

Use headphones while running the loopback to prevent acoustic feedback. Mobile applications must also provide the platform microphone permission: `RECORD_AUDIO` plus runtime permission on Android, and a microphone usage description on Apple platforms.

In the clean current-template [`GlistApp-TeamVoice`](examples/GlistApp-TeamVoice) example, select `[3]` to start the Opus microphone loopback. `Space` stops or restarts it, and `Esc` returns to the menu.

On Windows, configure, build and launch the microphone test from the repository root with:

```powershell
& .\examples\GlistApp-TeamVoice\run-team-voice.ps1
```

## Network Team Voice

`gTeamVoice` provides low-latency Opus microphone capture, per-speaker jitter buffering and mixed playback. `gTeamVoiceServer` relays each uplink only to server-authorized peers in the same server-owned team. Clients cannot select their sender identity, team or recipients in voice packets.

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

Register the three packet serializers with the same `znet::Codec` used by the game. On the server, call `gTeamVoiceServer::setPeerState()` only from authenticated game state whenever a player joins a match, changes team or changes voice permissions. Call `removePeer()` on disconnect.

See [TEAM_VOICE.md](TEAM_VOICE.md) for complete server/client handlers, lifecycle rules, threading, protocol behavior and diagnostics.

### Two-Computer Voice Test

Build and launch the example on both Windows computers:

```powershell
& .\examples\GlistApp-TeamVoice\run-team-voice.ps1
```

On the host computer, choose `[1]`, keep bind IP `0.0.0.0`, and use port `25000`. Allow the application through Windows Firewall on private networks; ZDT uses UDP. On the other computer, choose `[2]`, enter the host's LAN IPv4 address, and use the same port.

Wait until the HUD says `Voice: LISTENING`. Hold `V` only on the computer that is speaking. The other computer receives and plays voice automatically without pressing a key. Releasing `V` stops only that computer's microphone uplink; listening remains active. Use `Esc` to return to the menu and shut down the sessions cleanly.

The example deliberately assigns every ready connection to server-owned team `1`, which is suitable for this test but is not authentication. A production game must call `gTeamVoiceServer::setPeerState()` from its authenticated player, match, team and permission model.

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

Configure and run the standalone C++17 voice suite:

```bash
cmake -S tests -B build/voice-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/voice-tests
ctest --test-dir build/voice-tests --output-on-failure
```

The suite covers packet bounds, authoritative routing, rate limits, Opus encode/decode, jitter/reorder/loss/PLC behavior, lifecycle barriers and an encrypted four-client ZDT isolation smoke test.

To test the replication example manually, run two instances of the example app. In the first, choose Host (binds to `0.0.0.0`). In the second, choose Client and connect to `127.0.0.1` (or the host's LAN IP for cross-machine testing). Both use port `25000` by default.
