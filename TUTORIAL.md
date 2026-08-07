# Team Voice Tutorial

This guide walks you from a clean machine to a working two-way team voice test, then shows how to continue with your own game.

It uses the included `examples/GlistApp-TeamVoice` application. The example follows the current GlistApp template and uses one application-defined `gCanvas`.

## What You Will End Up With

- A working host/client team voice demo.
- A local Opus microphone loopback test.
- A clear path to use `gTeamVoice` and `gTeamVoiceServer` in your own project.

## Before You Start

- A Glist workspace containing `GlistEngine` and `glistplugins`.
- **CMake 3.29 or newer**.
- On Windows, the Glist toolchain includes OpenSSL. On Linux and macOS, install OpenSSL from your package manager.
- Internet access for the first configure; znet, zstd, gipOpus, and libopus are fetched automatically.
- Headphones for testing; speakers can cause acoustic feedback.
- For LAN testing, allow the app through the host firewall. ZDT uses UDP.

## 1. Put the Plugin in `glistplugins`

From your workspace:

```bash
cd path/to/your/glistplugins
git clone https://github.com/GlistPlugins/gipMultiplayer.git
```

Your layout should look like this:

```text
glist/
  GlistEngine/
  glistplugins/
    gipMultiplayer/
```

## 2. Add the Plugin to Your Project

In your project's `CMakeLists.txt`:

```cmake
set(PLUGINS gipMultiplayer)
```

The plugin pulls in znet and gipOpus automatically. You do not need to add them separately.

Include the public headers in your code:

```cpp
#include "gipMultiplayer.h"
```

For voice specifically:

```cpp
#include "audio/gTeamVoice.h"
#include "voice/gTeamVoiceServer.h"
```

## 3. Build and Run the Example

From the `gipMultiplayer` repository root on Windows:

```powershell
& .\examples\GlistApp-TeamVoice\run-team-voice.ps1
```

The script configures, builds, and starts the app.

## 4. Test the Audio Pipeline Locally

In the app menu:

1. Press `[3]` for local Opus microphone loopback.
2. Speak into the microphone.
3. You should hear yourself after about two seconds.
4. Press `Space` to stop or restart loopback.
5. Press `Esc` to return to the menu.

This confirms microphone capture, Opus encode/decode, and playback are working.

## 5. Run a Two-Instance Voice Test

Start two app instances.

### Host instance

1. Press `[1]`.
2. Keep bind IP as `0.0.0.0`.
3. Keep port as `25000`.
4. Wait until the voice screen appears.

### Client instance

1. Press `[2]`.
2. Enter `127.0.0.1` if both apps are on the same computer.
3. Keep port as `25000`.
4. Wait until the voice state becomes `LISTENING`.

### Push-to-talk

- Hold `V` on the computer that is speaking.
- The other side hears audio without pressing any key.
- Release `V` to stop transmitting.

If this works, the server-authoritative team voice path is healthy.

## 6. Continue in Your Own Project

Use the example as the reference implementation.

### On the server

- Create one `gTeamVoiceServer`.
- Call `addPeer(session)` after a live ZDT session exists.
- Call `setPeerState()` from authenticated server state when a player joins, changes team, or changes voice permission.
- Call `removePeer()` on disconnect.

### On the client

- Create one `gTeamVoice`.
- Call `initialize()` after the GlistEngine sound system is ready.
- Register voice serializers on the session codec.
- Forward session and downlink packets to `gTeamVoice`.
- Call `updateNetwork(session)` every frame.
- Use `startTransmitting()` and `stopTransmitting()` for push-to-talk.
- Call `shutdown()` before the audio system is destroyed.

For exact integration code and lifecycle rules, see [TEAM_VOICE.md](TEAM_VOICE.md).

## 7. Common Problems

### No audio device or initialization failure

Check that the default microphone and playback devices exist and are usable. On Windows, verify microphone privacy settings.

### The client connects but stays on `WAITING FOR SERVER`

The server has not yet published voice state for that peer. In the example, the server assigns ready peers to team `1`; in your game, make sure authenticated server logic calls `setPeerState()`.

### One side hears nothing

Confirm only one side is holding `V`, both sides show `LISTENING`, and the server is relaying packets.

### Windows Firewall blocks LAN traffic

Allow the app on private networks, then retry.

## Next Steps

- Read [README.md](README.md) for package setup and test commands.
- Read [TEAM_VOICE.md](TEAM_VOICE.md) for packet registration, lifecycle rules, threading, and diagnostics.
- Use `examples/GlistApp-TeamVoice` as the working sample when wiring voice into your own game.
