# gipMultiplayer Roadmap

This document outlines the planned development direction for gipMultiplayer. Items are grouped by priority and category.

---

## 🔴 High Priority — Must Do Next

### 1. CI/CD Pipeline (GitHub Actions)
**Why:** Catch regressions early; verify builds on every PR.
**Scope:**
- **Windows** (MSVC + MinGW): build plugin, example app, run voice tests
- **Linux** (Ubuntu): build plugin, run tests
- **Android** (NDK): build plugin (optional: run on emulator)
- Run on every PR and push to `main`
- Cache dependencies (zstd, opus, OpenSSL)

### 2. Android Platform Integration
- Runtime `RECORD_AUDIO` permission request helper (Java/Kotlin bridge)
- ProGuard rules for znet/Opus
- Gradle integration docs for gipMultiplayer

### 3. iOS/macOS Platform Integration
- `NSMicrophoneUsageDescription` guidance
- CocoaPods / Swift Package Manager notes
- Audio session category setup (playAndRecord, mixWithOthers)

---

## 🟡 Medium Priority — Voice Quality & UX

### 4. Echo Cancellation (AEC)
- Integrate WebRTC AECM (Acoustic Echo Cancellation Mobile)
- Enable when headphones not detected
- Configurable: `voice.setEchoCancellation(true)`

### 5. Voice Activity Detection (VAD)
- WebRTC VAD or Opus built-in DTX
- "Voice activation" mode: transmit only when speaking (no PTT key)
- Configurable sensitivity

### 6. Adaptive Bitrate / Opus Features
- DTX (Discontinuous Transmission) toggle
- FEC (Forward Error Correction) toggle
- DRED (Redundant Audio) toggle
- Dynamic bitrate based on packet loss / RTT

### 7. Jitter Buffer Tuning UI
- Expose `initial_jitter_packets`, `max_jitter_packets` in example
- Visual jitter / packet loss graph in demo app

---

## 🟢 Lower Priority — Example & Ecosystem

### 8. `GlistApp-TeamVoice` Enhancements
- Lobby / room list (via Master Server)
- Player list with per-player mute / volume sliders
- Text chat overlay (demo)
- Settings screen: mic gain, output device, push-to-talk key bind

### 9. Documentation Site
- MkDocs / GitHub Pages for:
  - Getting started
  - API reference (Doxygen)
  - Platform-specific guides
  - Migration guides (znet 3.x → 4.0)

---

## How to Contribute

1. Pick an item from **High/Medium** priority
2. Create a branch: `feature/<short-name>`
3. Open a draft PR early for discussion
4. Ensure:
   - Release + Debug builds pass
   - All existing tests pass
   - Example app runs on Windows
   - No new compiler warnings
5. Request review

---

*Last updated: 2026-08-07*