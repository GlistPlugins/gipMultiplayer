# Team Voice Tutorial

Getting from a fresh checkout to a working two-way voice test with the [`GlistApp-TeamVoice`](examples/GlistApp-TeamVoice) example, then wiring voice into your own game.

## Before You Start

- A Glist workspace containing `GlistEngine` and `glistplugins`.
- CMake 3.29 or newer.
- OpenSSL. The Glist toolchain ships it on Windows, install it from your package manager on Linux and macOS.
- Internet access for the first configure, which fetches znet, zstd, gipOpus and libopus.
- Headphones, speakers cause acoustic feedback.
- For LAN testing, allow the app through the host firewall. ZDT runs over UDP.

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

znet and gipOpus come along with it, you do not need to add them separately.

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

The script configures, builds and starts the app.

## 4. Test the Audio Pipeline Locally

Press `[3]` in the menu for local Opus loopback and speak into the microphone. You should hear yourself after about two seconds, which confirms capture, encode/decode and playback all work. `Space` stops or restarts the loopback, `Esc` returns to the menu.

## 5. Run a Two-Instance Voice Test

Start two instances of the app.

On the host, press `[1]`, keep bind IP `0.0.0.0` and port `25000`, and wait for the voice screen.

On the client, press `[2]`, enter `127.0.0.1` if both apps are on the same computer or the host's LAN IPv4 address otherwise, keep port `25000`, and wait for the state to become `LISTENING`.

Hold `V` on the speaking side. The other side hears audio without pressing any key. Release `V` to stop transmitting.

## 6. Continue in Your Own Project

Use the example as the reference implementation.

On the server:

- Create one `gTeamVoiceServer`.
- Call `addPeer(session)` once a live ZDT session exists.
- Call `setPeerState()` from authenticated server state when a player joins, changes team or changes voice permission.
- Call `removePeer()` on disconnect.

On the client:

- Create one `gTeamVoice`.
- Call `initialize()` after the GlistEngine sound system is ready.
- Register the voice serializers on the session codec.
- Forward session and downlink packets to `gTeamVoice`.
- Call `updateNetwork(session)` every frame.
- Use `startTransmitting()` and `stopTransmitting()` for push-to-talk.
- Call `shutdown()` before the audio system is destroyed.

[TEAM_VOICE.md](TEAM_VOICE.md) has the exact integration code, lifecycle rules, threading and diagnostics.

## 7. Common Problems

**Initialization fails or no audio device is found.** Check that the default microphone and playback devices exist and are usable. On Windows, check the microphone privacy settings.

**The client connects but stays on `WAITING FOR SERVER`.** The server has not published voice state for that peer yet. The example assigns ready peers to team `1`; in your game, make sure authenticated server logic calls `setPeerState()`.

**One side hears nothing.** Confirm only one side is holding `V`, that both sides show `LISTENING`, and that the server is relaying packets.

**Windows Firewall blocks LAN traffic.** Allow the app on private networks, then retry.
