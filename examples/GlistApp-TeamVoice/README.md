# GlistApp Team Voice

A GlistApp example with a single `gCanvas`, demonstrating local Opus loopback and two-way server-authoritative team voice.

## Run on Windows

From the gipMultiplayer repository root:

```powershell
& .\examples\GlistApp-TeamVoice\run-team-voice.ps1
```

## Controls

- `[1]`: host team voice
- `[2]`: join team voice
- `[3]`: local Opus microphone loopback
- `V`: hold to transmit, receiving is always active
- `Space`: stop or restart local loopback
- `Esc`: disconnect and return to the menu

For a LAN test, host on `0.0.0.0:25000` and connect the other computer to the host's LAN IPv4 address on the same port. ZDT runs over UDP, so allow the app through the host firewall.

Every protocol-ready connection is assigned to server-owned team `1`. Production games must feed authenticated player, match, team and permission state to `gTeamVoiceServer::setPeerState()` instead.
