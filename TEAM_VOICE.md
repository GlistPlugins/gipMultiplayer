# Team Voice Integration

`gTeamVoice` and `gTeamVoiceServer` implement server-authoritative team voice over znet's encrypted ZDT transport. Audio is 48 kHz mono signed 16-bit PCM, encoded as 20 ms Opus frames at 24 kbit/s.

For a guided first run, see [TUTORIAL.md](TUTORIAL.md).

## Data Flow

```text
microphone callback -> bounded PCM queue -> Opus worker -> uplink packet
uplink packet -> authoritative server router -> same-team downlink packet
downlink packet -> per-speaker jitter/Opus worker -> mixer -> playback queue
```

The audio callback only moves PCM through bounded queues. Encoding, decoding, packet reordering, packet-loss concealment and mixing run on the voice worker.

## Codec Registration

Register the voice serializers on every client and server session, alongside the game's packet serializers:

```cpp
auto codec = std::make_shared<znet::Codec>();
registerGamePackets(*codec);
gRegisterTeamVoicePackets(*codec, [](gTeamVoicePacketError error) {
	gLoge(std::string("Malformed voice packet: ") + gGetTeamVoicePacketErrorString(error));
});
session->SetCodec(codec);
```

Use the malformed-packet callback for telemetry or connection policy. The serializers reject unsupported versions, invalid flags and generations, truncated/trailing data, empty payloads, and Opus payloads larger than 256 bytes.

## Server Integration

Create one router for the game server. The packet handler supplies the connection ID; the packet itself deliberately contains no player or team identity.

```cpp
class ServerVoiceHandler
		: public znet::PacketHandler<ServerVoiceHandler, gTeamVoiceUplinkPacket> {
public:
	ServerVoiceHandler(gTeamVoiceServer& router, std::uint64_t connectionid)
			: router(router), connectionid(connectionid) {}

	void OnPacket(std::shared_ptr<gTeamVoiceUplinkPacket> packet) {
		router.handleVoicePacket(connectionid, *packet);
	}

private:
	gTeamVoiceServer& router;
	std::uint64_t connectionid;
};
```

Add a peer only after znet has produced a live ZDT session:

```cpp
gTeamVoiceServer voiceRouter;

voiceRouter.addPeer(session);
session->SetHandler(std::make_shared<ServerVoiceHandler>(voiceRouter, session->id()));
```

Publish routing state from authenticated server game state:

```cpp
gTeamVoiceServer::PeerState state;
state.playerid = authenticatedPlayerId;
state.teamid = authoritativeTeamId;
state.sessionid = authoritativeMatchId;
state.cantransmit = playerMaySpeak;
state.canreceive = playerMayListen;

if (!voiceRouter.setPeerState(session->id(), state)) {
	// The reliable control packet could not be queued. Retry or disconnect.
}
```

Call `setPeerState()` again when the player changes match, team, player identity, or permissions. The router increments a membership generation and sends a reliable control packet before accepting media for that state. A failed control send is not published and the same call can be retried without changing generation.

On leave and disconnect:

```cpp
voiceRouter.clearPeerState(session->id());
voiceRouter.removePeer(session->id());
```

Never derive `PeerState` from values supplied in a voice packet. In particular, `sessionid` is a server-owned match/voice-session identifier, not a client-selected room.

### Rate Limits

Each sender has independent packet and byte token buckets. Defaults allow normal 20 ms voice while bounding abuse:

```cpp
gTeamVoiceServer::Config config;
config.maxconnections = 64;
config.packetspersecond = 65.0;
config.packetburst = 20.0;
config.bytespersecond = 24576.0;
config.byteburst = 8192.0;
gTeamVoiceServer voiceRouter(config);
```

## Client Integration

Own one `gTeamVoice` for the local player. Initialize it after GlistEngine's sound engine is available and shut it down before the engine audio system is destroyed:

```cpp
gTeamVoice voice;

if (!voice.initialize()) {
	gLoge("Voice initialization failed: " + voice.getLastError());
}

// During orderly application shutdown:
voice.shutdown();
```

Forward session controls and downlinks from the client's znet packet handler:

```cpp
class ClientVoiceHandler : public znet::PacketHandler<ClientVoiceHandler,
		gTeamVoiceSessionPacket, gTeamVoiceDownlinkPacket> {
public:
	explicit ClientVoiceHandler(gTeamVoice& voice) : voice(voice) {}

	void OnPacket(std::shared_ptr<gTeamVoiceSessionPacket> packet) {
		voice.handleSessionPacket(*packet);
	}

	void OnPacket(std::shared_ptr<gTeamVoiceDownlinkPacket> packet) {
		voice.handleVoicePacket(*packet);
	}

private:
	gTeamVoice& voice;
};
```

Pump outgoing packets regularly while connected:

```cpp
void update() {
	if (session && session->IsAlive()) voice.updateNetwork(*session);
}
```

`updateNetwork()` returns the number of packets the session accepted.

Push-to-talk:

```cpp
void onPushToTalkPressed() {
	voice.startTransmitting();
}

void onPushToTalkReleased() {
	voice.stopTransmitting();
}
```

Transmission starts disabled. `setEnabled(false)`, `stopTransmitting()`, `setLocalMuted(true)`, session changes and shutdown invalidate queued uplinks, so stale media is not sent afterward.

Push-to-talk controls transmission only. Receiving needs no key: authorized downlinks are decoded and played continuously while voice is enabled.

On network disconnect, call `resetSession()`. The next server control packet establishes the new session:

```cpp
voice.stopTransmitting();
voice.resetSession();
```

Per-speaker controls use the authoritative `playerid` carried by downlinks:

```cpp
voice.setSpeakerMuted(playerid, true);
voice.setSpeakerVolume(playerid, 0.75f); // Clamped to 0.0 through 2.0.
```

## Threading

- znet packet callbacks may call `handleSessionPacket()` and `handleVoicePacket()` from network threads.
- The game update thread should call `updateNetwork()` and push-to-talk methods.
- Client lifecycle and network sending are serialized internally so disable/session transitions are media barriers.
- `gTeamVoiceServer` supports packet handling and authoritative state updates from different threads.
- Do not perform codec work in an audio or znet callback; the supplied classes already queue it to the worker.

## Transport and Playout

- Session controls use reliable, ordered channel 1.
- Opus uplinks and downlinks use unreliable, unordered channel 2.
- Sequence numbers are wrap-safe. Reordering is handled by each speaker's jitter buffer, not by transport ordering.
- The default initial jitter is four packets (80 ms), with a twelve-packet bound.
- Missing packets use Opus PLC for at most five consecutive frames.
- Inactive speaker decoders are removed after two seconds.
- Up to 32 speakers are active by default.

Voice runs unreliable, so packet loss is normal. Health checks should look at whether audio decodes and stays isolated to the right team, not at whether every sent packet arrived.

## Diagnostics

Client-wide counters are available through `gTeamVoice::getStats()`. Per-speaker jitter, loss, PLC, decode, mute, volume, and activity data are available through `getSpeakerStats()`.

Server counters are available through `gTeamVoiceServer::getStats()` and include received/relayed bytes and packets, malformed input, routing rejection, rate limiting, and send failure.

## Platform Permissions

Windows and desktop Linux/macOS builds use the default capture and playback devices. Mobile applications need platform microphone permission before `initialize()`:

- Android: declare `RECORD_AUDIO` and request it at runtime.
- Apple platforms: provide the microphone usage description the target platform requires.

Use headphones during development to prevent acoustic feedback.

## Example and Tests

[`examples/GlistApp-TeamVoice`](examples/GlistApp-TeamVoice) covers host, client, local Opus loopback, diagnostics and push-to-talk in one `gCanvas`. See its [README](examples/GlistApp-TeamVoice/README.md) for the controls and [README.md](README.md) for the standalone test suite.
