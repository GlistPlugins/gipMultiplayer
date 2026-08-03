# GlistApp Team Voice

This is a clean current-template GlistApp example with one application-defined `gCanvas`. It demonstrates local Opus loopback and two-way server-authoritative team voice without depending on the legacy multiplayer sample.

## Run on Windows

From the gipMultiplayer repository root:

```powershell
& .\examples\GlistApp-TeamVoice\run-team-voice.ps1
```

## Controls

- `[1]`: host team voice
- `[2]`: join team voice
- `[3]`: local Opus microphone loopback
- `V`: hold to transmit; receiving is always active
- `Esc`: disconnect and return to the menu
- `Space`: stop or restart local loopback

For a LAN test, host on `0.0.0.0:25000` and connect the other computer to the host's LAN IPv4 address on port `25000`. ZDT uses UDP, so allow the application through the host firewall.

The example assigns every protocol-ready connection to server-owned team `1`. Production games must provide authenticated player, match, team, and permission state to `gTeamVoiceServer::setPeerState()`.
