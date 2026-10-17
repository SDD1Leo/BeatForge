# BeatForge architecture

## Layout

```
BeatForge/
├── CMakeLists.txt, CMakePresets.json
├── external/JUCE/        JUCE 9.0.4 (git submodule, pinned tag)
├── core/                 beatforge_core: GUI-free static library
│   ├── include/beatforge/  public headers
│   └── src/audio/          AudioEngine, TestTone, LevelMeter (pure C++, no JUCE)
├── app/                  BeatForge GUI executable (JUCE)
│   └── Source/
│       ├── Main.cpp        application, main window, smoke-test mode
│       ├── MainComponent.* root content: header, transport, master meter, status
│       ├── audio/          AudioDeviceController (JUCE AudioDeviceManager <-> engine)
│       └── ui/             theme, LookAndFeel, TransportBar, LevelMeterComponent, StatusBar
├── tests/                Catch2 unit tests + app launch test (CTest)
└── mcp/                  (Phase 10) TypeScript MCP server
```

## Components (target design)

| Component | Lives in | Arrives |
|---|---|---|
| A. Application & GUI | `app/` | Phase 0+ |
| B. Project model | `core/` | Phase 3–4 |
| C. Transport engine (musical clock, PPQ 960) | `core/` | Phase 1–2 |
| D. MIDI engine | `core/` | Phase 5, 8 |
| E. Audio engine (device, instruments, mixer) | `core/` + JUCE audio modules | Phase 1+ |
| F. Offline renderer | `core/` | Phase 9 |
| G. Command API (single mutation path) | `core/` | grows each phase; formalised by Phase 10 |
| H. MCP server (TypeScript, local IPC) | `mcp/` | Phase 10 |

Rules that hold from day one:

- **`core/` never depends on GUI code.** Anything testable without a window
  goes there. The app links `core`; tests link `core`.
- **One mutation path.** GUI, MCP and a future AI assistant all change the
  project through the command API — never by poking widgets or engine memory.
- **The audio callback is real-time safe.** No locks shared with the GUI, no
  allocation, no file/network/IPC, no waiting. UI ↔ audio communication uses
  lock-free queues / atomics (introduced in Phase 1).
- **Timing comes from the audio timeline**, never from UI timers or frame rate.
- **Live playback and offline export share** the same instrument and mixing code.

## Audio path (Phase 1)

```
 GUI thread                               audio thread (device callback)
 ──────────                               ──────────────────────────────
 TransportBar ── play()/stop() ──► atomic<bool> playRequested
                                         │
 AudioDeviceController                   ▼
   AudioDeviceManager ──callback──► AudioEngine::process(outputs, n)
   (ALSA → PipeWire)                       ├─ TestTone (10 ms click-free ramp)
                                           ├─ position, state  ──► atomics ──► TransportBar (30 Hz poll)
                                           └─ LevelMeter peaks ──► atomic max ──► LevelMeterComponent (30 Hz)
```

- `AudioEngine` is plain C++ in `core/`; tests drive it with fake buffers.
- The callback path touches only atomics and pre-sized state — no locks, no
  allocation (enforced by a unit test that counts heap allocations).
- `audioDeviceAboutToStart` → `engine.prepare(sampleRate)`;
  `audioDeviceStopped` → `engine.reset()` (silence, rewind). Changing device,
  rate or buffer size restarts the device, which stops playback.
- Device errors may arrive on any thread; they are stored under a mutex that is
  never taken on the audio path, then shown via an async update.

## Toolchain decisions

- **C++20** (g++ 13.3) — supported by JUCE 9 and our toolchain.
- **JUCE 9.0.4** — current supported release; pinned as a submodule for
  reproducible builds. WebKit/curl disabled (not needed).
- **Ninja + CMake presets** — `debug` / `release`, build dirs under `build/`.
- **Catch2 v3.16.0** via FetchContent for C++ unit tests.
- **Audio on Linux:** ALSA, whose default device is PipeWire. JUCE's JACK
  backend is disabled (`JUCE_JACK=0`): Ubuntu's `libjack` is jackd2's, so
  without a JACK server it only prints connection errors. Revisit with
  `pw-jack` if we need lower latency than ALSA-via-PipeWire gives.
