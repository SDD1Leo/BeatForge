# BeatForge architecture

## Layout

```
BeatForge/
├── CMakeLists.txt, CMakePresets.json
├── external/JUCE/        JUCE 9.0.4 (git submodule, pinned tag)
├── core/                 beatforge_core: GUI-free static library
│   ├── include/beatforge/  public headers
│   └── src/
│       ├── audio/          AudioEngine, DrumMachine, DrumKit, PolySynth, Metronome, LevelMeter, PreviewQueue
│       ├── dsp/            Biquad filter, deterministic Noise (pure C++, no JUCE)
│       ├── midi/           Note/Clip model, quantize, NoteSchedule + lock-free ScheduleExchange
│       └── project/        Project model, ProjectFile (JSON), Session (single mutation path)
│       └── time/           MusicalTime (PPQ 960, bar/beat/tick), TempoMap, Transport
├── app/                  BeatForge GUI executable (JUCE)
│   └── Source/
│       ├── Main.cpp        application, main window, smoke-test mode
│       ├── MainComponent.* root content: header, transport, master meter, status
│       ├── audio/          AudioDeviceController (JUCE AudioDeviceManager <-> engine)
│       ├── project/        ProjectActions (file commands, dialogs, recent files)
│       └── ui/             theme, LookAndFeel, TransportBar, ProjectButton, SignatureField, TempoField,
│                           TimelineRuler, DrumMachinePanel, PianoRollPanel, TabBar,
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

## MIDI (Phase 5)

```
 PianoRoll ─► Session::addNote/updateNotes/... ─► Project.synthClip
                       │
                       └─► AudioEngine::setMidiClip ─► NoteSchedule::build (message thread)
                                                        │  sorted on/off events, offs first
                                                        ▼
                         ScheduleExchange: pending ──acquire()──► current (audio thread)
                                           retired ◄──────────────┘  freed later by collectGarbage()
```

- **Note** = id, pitch, velocity, start tick, length (clip-local). The clip
  loops from bar 1 every `length` ticks. Quantize moves starts to the nearest
  grid line (ties later), keeps lengths, keeps notes inside the clip.
- **Hand-over without locks or frees on the audio thread:** the message
  thread publishes an immutable schedule; the audio thread swaps it in at a
  block start and parks the old one in a "retired" slot that only the message
  thread empties. A newer schedule waits until that slot is free.
- **Sample-exact scheduling:** the transport reports each block as segments
  with an event-tick window `(eventsAfter, eventsUpTo]`, the exact inverse of
  "first sample at or after the tick". Bounds are carried from segment to
  segment, so every event has exactly one owner across blocks, tempo changes
  and loop wraps (stress-tested with random tempo changes at block sizes
  1/37/64/4096: bit-identical output).
- Within a block, note on/off, loop jumps and grid lines (drums, metronome)
  are merged in sample order (offs, jumps, grid, ons) and voices are rendered
  between them.
- **No stuck notes:** voices are keyed by note id; on a schedule change, a loop
  wrap, or Stop, voices whose note no longer covers the playhead are released.
- **PolySynth** (16 voices): PolyBLEP saw, one-pole low-pass, linear ADSR,
  velocity gain, voice stealing. A placeholder sound until Phase 6.
- Editor auditions go through a lock-free SPSC `PreviewQueue`.

## Projects and the command path (Phase 4)

```
 GUI widgets ──► Session (core) ──► AudioEngine atomics
 (later: MCP, assistant)   │
                           ├─► Project (plain data) ──► ProjectFile ⇄ .beatforge JSON
                           └─► listeners (UI refresh, title, saved state)
```

- **`Session`** is the only way to change the project: it validates, updates
  the `Project`, pushes the change into the engine and notifies listeners
  (`replaced`, `transport`, `pattern`, `lanes`, `metadata`). It tracks the file
  path and the dirty flag. Message-thread only. MCP (Phase 10) will call the
  same methods.
- Auditioning a drum is not an edit, so it goes straight to the engine.
- **`ProjectActions`** (app) adds file dialogs, prompts, recent files and
  error reporting on top of the Session. In `--smoke-test` mode it prints
  results instead of opening dialogs and never touches recent-file settings.

### File format v1 (`.beatforge`, UTF-8 JSON)

```json
{ "format": "beatforge-project", "version": 1, "savedBy": "BeatForge 0.4.0",
  "name": "Night Drive", "tempo": 124.0,
  "timeSignature": { "numerator": 4, "denominator": 4 },
  "metronome": false, "loop": { "enabled": false, "startBar": 1, "bars": 4 },
  "tracks": [ { "type": "drumMachine", "name": "Drums", "lanes": [
      { "sound": "kick", "name": "808 Kick", "gainDb": 0.0, "muted": false,
        "soloed": false, "steps": "x...x...x...x..." } ] } ] }
```

Synth track (added in 0.5, still format version 1 — 0.4 keeps it as an
unknown track):

```json
{ "type": "instrument", "instrument": "basicSynth", "name": "Synth",
  "clip": { "lengthTicks": 15360,
            "notes": [ { "pitch": 60, "velocity": 100, "start": 0, "length": 480 } ] } }
```

Loading rules:
- Not JSON, wrong `format`, or missing `version` → refused with a reason.
- `version` newer than this build → refused ("saved by a newer BeatForge"),
  never half-read.
- Out-of-range or wrong-typed values → corrected, each with a warning shown to
  the user; nothing changes on disk until they save.
- Unknown keys, extra steps beyond 16, unknown lanes and track types,
  unknown note keys and unreadable notes → kept and written back on save (no
  silent data loss). Out-of-range pitch/velocity is limited with a warning.
- Notes beyond a shortened clip stay in the project (silent, hidden).
- Save = write `<file>.tmp`, then rename over the target.

## Drum machine (Phase 3)

- **Grid:** the transport emits every 16th-note grid line (`stepTicks` = 240)
  flagged `isStep` / `isBeat`; step index = (tick / 240) mod 16, so the
  pattern repeats every 3840 ticks (one 4/4 bar) from bar 1.
- **Kit:** `DrumKit::generate(sampleRate)` synthesises the five one-shots into
  buffers (deterministic: fixed-seed noise, so renders are repeatable). It
  runs in `prepare()`, never on the audio thread. Peak levels are set so all
  five drums on one step stay below 0 dBFS (tested). Playback is sample-based,
  so user samples can replace a slot later.
- **Voices:** two per lane; a retrigger fades the previous hit over 1.5 ms (no
  clicks). The closed hat chokes the open hat (5 ms fade).
- **Controls:** pattern steps, lane volume/mute/solo and audition requests are
  atomics written by the UI. Gain changes ramp linearly over a fixed 5 ms,
  independent of buffer size.
- **Timing:** the engine renders voices segment-by-segment between grid lines,
  so every hit starts on its exact sample; output is bit-identical for any
  block size (tested 1…4096).
- **Stop** rewinds immediately; ringing tails (drums, metronome) finish
  naturally. Auditions play while stopped.

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
   (ALSA → PipeWire)                       ├─ Transport grid → Metronome clicks + DrumMachine steps
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
