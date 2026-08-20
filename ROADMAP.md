# gipMultiplayer Roadmap

Planned work, grouped by priority.

## High Priority

### 1. CI/CD pipeline (GitHub Actions)

Catches regressions early and verifies builds on every PR and push to `main`.

- Windows (MSVC and MinGW): build plugin, build example app, run voice tests
- Linux (Ubuntu): build plugin, run tests
- Android (NDK): build plugin, optionally run on an emulator
- Cache dependencies (zstd, opus, OpenSSL)

### 2. Android platform integration

- Runtime `RECORD_AUDIO` permission request helper (Java/Kotlin bridge)
- ProGuard rules for znet and Opus
- Gradle integration docs

### 3. iOS/macOS platform integration

- `NSMicrophoneUsageDescription` guidance
- CocoaPods and Swift Package Manager notes
- Audio session category setup (playAndRecord, mixWithOthers)

## Medium Priority: Voice Quality and UX

### 4. Echo cancellation

- Integrate WebRTC AECM
- Enable when headphones are not detected
- Configurable through `voice.setEchoCancellation(true)`

### 5. Voice activity detection

- WebRTC VAD or Opus built-in DTX
- Voice activation mode: transmit only when speaking, no push-to-talk key
- Configurable sensitivity

### 6. Adaptive bitrate and Opus features

- DTX (discontinuous transmission) toggle
- FEC (forward error correction) toggle
- DRED (redundant audio) toggle
- Dynamic bitrate based on packet loss and RTT

### 7. Jitter buffer tuning

- Expose `initial_jitter_packets` and `max_jitter_packets` in the example
- Jitter and packet loss graph in the demo app

## Lower Priority: Example and Ecosystem

### 8. `GlistApp-TeamVoice` enhancements

- Lobby and room list through the master server
- Player list with per-player mute and volume sliders
- Text chat overlay
- Settings screen: mic gain, output device, push-to-talk key bind

### 9. Documentation site

MkDocs or GitHub Pages covering getting started, a Doxygen API reference, platform-specific guides and migration guides (znet 3.x to 4.0).

## How to Contribute

1. Pick an item from high or medium priority.
2. Create a branch named `feature/<short-name>`.
3. Open a draft PR early for discussion.
4. Check that Release and Debug builds pass, existing tests pass, the example app runs on Windows, and no new compiler warnings appear.
5. Request review.
