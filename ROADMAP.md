# gipMultiplayer Roadmap

This document outlines the planned development direction for gipMultiplayer. Items are grouped by priority and category.

---

## ✅ Recently Completed (merged to `main`)

| Item | PR | Description |
|------|----|-------------|
| ZNet 4.0 API migration | #9 | Namespace changes, `Result` type, updated all call sites |
| Master Server framework | #10 | Dedicated server, server browser, master server registration |
| Team Voice + single-gCanvas example | #11 | Server-authoritative Opus voice, TUTORIAL.md, docs |

---

## 🔴 High Priority — Must Do Next

### 1. Vendor znet locally (`libs/znet/`, `libs/zstd/`)
**Why:** Professor requested to stop FetchContent at configure time. External dependency updates break our build unexpectedly.
**Approach:**
- Copy znet source (pinned commit) to `libs/znet/`
- Copy zstd source (pinned commit `48c0ed7`) to `libs/zstd/`
- Replace `external/znet.cmake` with `add_subdirectory` based version
- Keep OpenSSL handling (Windows/Android) and GCC 15+ workaround
- Remove all `FetchContent` calls from the plugin
- Keep `FETCHCONTENT_SOURCE_DIR_ZNET` override for local development

**Acceptance:**
- `cmake -S . -B build` works without internet on first run
- Release/Debug builds pass
- Android build passes
- Example app runs end-to-end

---

## 🟠 Medium Priority — Stability & Platform Support

### 2. CI/CD Pipeline (GitHub Actions)
- **Windows** (MSVC + MinGW): build plugin, example app, run voice tests
- **Linux** (Ubuntu): build plugin, run tests
- **Android** (NDK): build plugin (optional: run on emulator)
- Run on every PR and push to `main`
- Cache dependencies (zstd, opus, OpenSSL)

### 3. Android Platform Integration
- Runtime `RECORD_AUDIO` permission request helper (Java/Kotlin bridge)
- ProGuard rules for znet/Opus
- Gradle integration docs for gipMultiplayer

### 4. iOS/macOS Platform Integration
- `NSMicrophoneUsageDescription` guidance
- CocoaPods / Swift Package Manager notes
- Audio session category setup (playAndRecord, mixWithOthers)

---

## 🟡 Lower Priority — Voice Quality & UX

### 5. Echo Cancellation (AEC)
- Integrate WebRTC AECM (Acoustic Echo Cancellation Mobile)
- Enable when headphones not detected
- Configurable: `voice.setEchoCancellation(true)`

### 6. Voice Activity Detection (VAD)
- WebRTC VAD or Opus built-in DTX
- "Voice activation" mode: transmit only when speaking (no PTT key)
- Configurable sensitivity

### 7. Adaptive Bitrate / Opus Features
- DTX (Discontinuous Transmission) toggle
- FEC (Forward Error Correction) toggle
- DRED (Redundant Audio) toggle
- Dynamic bitrate based on packet loss / RTT

### 6. Jitter Buffer Tuning UI
- Expose `initial_jitter_packets`, `max_jitter_packets` in example
- Visual jitter / packet loss graph in demo app

---

## 🟢 Optional — Example & Ecosystem

### 8. `GlistApp-TeamVoice` Enhancements
- Lobby / room list (via Master Server)
- Player list with per-player mute / volume sliders
- Text chat overlay (demo)
- Settings screen: mic gain, output device, push-to-talk key bind

### 9. gipZnet Plugin (Alternative to Vendoring)
If team prefers separate plugin:
- Create `glistplugins/gipZnet/` with vendored znet+zstd
- gipMultiplayer depends on `gipZnet`
- Disable auto-update in gipZnet (pinned commit)

### 10. Documentation Site
- MkDocs / GitHub Pages for:
  - Getting started
  - API reference (Doxygen)
  - Platform-specific guides
  - Migration guides (znet 3.x → 4.0)

---

## 📋 Tracking

| Item | Status | Target | Owner |
|------|--------|--------|-------|
| Vendor znet locally | 🔴 Planned | Next sprint | — |
| CI/CD Pipeline | 🟠 Planned | After vendoring | — |
| Android permission helper | 🟠 Planned | After CI | — |
| iOS platform guide | 🟠 Planned | After CI | — |
| Echo Cancellation (AEC) | 🟡 Backlog | — | — |
| Voice Activity Detection | 🟡 Backlog | — | — |
| Adaptive Opus features | 🟡 Backlog | — | — |
| Example enhancements | 🟢 Backlog | — | — |
| gipZnet plugin (alt) | 🟢 Backlog | — | — |
| Documentation site | 🟢 Backlog | — | — |

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

## Versioning

- **Patch** (x.y.Z): Bug fixes, docs, CI tweaks
- **Minor** (x.Y.z): New features (voice opts, platform helpers), backward compatible
- **Major** (X.y.z): Breaking API changes (e.g., znet 4.0 migration)

Target: one minor release per 2–3 months; patch as needed.

---

*Last updated: 2026-08-07*