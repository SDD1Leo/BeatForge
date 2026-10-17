# BeatForge architecture

## Layout

```
BeatForge/
├── CMakeLists.txt, CMakePresets.json
├── external/JUCE/        JUCE 9.0.4 (git submodule, pinned tag)
├── core/                 beatforge_core: GUI-free static library
│   ├── include/beatforge/  public headers
│   └── src/
│       ├── audio/          AudioEngine, Metronome, LevelMeter (pure C++, no JUCE)
│       └── time/           MusicalTime (PPQ 960, bar/beat/tick), TempoMap, Transport
├── app/                  BeatForge GUI executable (JUCE)
│   └── Source/
│       ├── Main.cpp        application, main window, smoke-test mode
│       ├── MainComponent.* root content: header, transport, master meter, status
│       ├── audio/          AudioDeviceController (JUCE AudioDeviceManager <-> engine)
│       └── ui/             theme, LookAndFeel, TransportBar, TempoField, TimelineRuler,
│                           LevelMeterComponent, StatusBar
├── tools/                analyze_onsets.py (timing analysis of captured audio)
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

## Musical clock (Phase 2)

- **Musical time is ticks** (960 per quarter note), independent of sample rate.
  `TimeSignature` gives ticks per beat/bar; `toBarBeatTick` formats positions.
- **`TempoMap`** (piecewise-constant tempo) converts ticks ↔ seconds ↔ samples
  and already supports multiple tempo changes; it will drive arrangement math
  and offline rendering. The live transport only needs the current tempo.
- **`Transport`** (audio thread only) never accumulates time. Position is
  `anchorTick + (sample − anchorSample) / samplesPerTick` from an integer sample
  counter; the anchor moves only on a tempo change or loop wrap. Result: no
  drift (tested over one hour at awkward tempos).
- **Event rule:** an event at tick T fires on the first sample whose position
  is ≥ T. Each beat fires exactly once regardless of block size, tempo changes
  or loop wraps (tested with block sizes 1…4096 and 300+ tempo changes).
- Loops are `[start, end)` in ticks; a wrap splits the block at the exact sample.
- `AudioEngine::process` collects the block's beats from the transport, then
  renders the metronome voice segment by segment so every click starts on its
  exact sample.
- UI → engine settings (tempo, signature, loop, metronome) are atomics read at
  the start of each block; engine → UI position/state are atomics polled at
  60 Hz for display only.

## Audio path (Phase 1)

```
 GUI thread                               audio thread (device callback)
 ──────────                               ──────────────────────────────
 TransportBar ── play()/stop() ──► atomic<bool> playRequested
                                         │
 AudioDeviceController                   ▼
   AudioDeviceManager ──callback──► AudioEngine::process(outputs, n)
   (ALSA → PipeWire)                       ├─ Transport → Metronome clicks (Phase 2)
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
