# BeatForge architecture

## Layout

```
BeatForge/
├── CMakeLists.txt, CMakePresets.json
├── external/JUCE/        JUCE 9.0.4 (git submodule, pinned tag)
├── core/                 beatforge_core: GUI-free static library
│   ├── include/beatforge/  public headers
│   └── src/
├── app/                  BeatForge GUI executable (JUCE)
│   └── Source/
│       ├── Main.cpp        application, main window, smoke-test mode
│       ├── MainComponent.* root content component
│       └── ui/             theme + LookAndFeel
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

## Toolchain decisions

- **C++20** (g++ 13.3) — supported by JUCE 9 and our toolchain.
- **JUCE 9.0.4** — current supported release; pinned as a submodule for
  reproducible builds. WebKit/curl disabled (not needed).
- **Ninja + CMake presets** — `debug` / `release`, build dirs under `build/`.
- **Catch2 v3.16.0** via FetchContent for C++ unit tests.
- **Audio on Linux:** JUCE talks to ALSA and JACK. On this machine both are
  served by PipeWire (`pipewire-alsa`, `pipewire-jack`).
