# BeatForge architecture

## Layout

```
BeatForge/
├── CMakeLists.txt, CMakePresets.json
├── external/JUCE/        JUCE 9.0.4 (git submodule, pinned tag)
├── core/                 beatforge_core: GUI-free static library
│   ├── include/beatforge/  public headers
│   └── src/
│       ├── audio/          AudioEngine (16 track slots + mixer), TrackSchedule, DrumMachine, DrumKit,
│       │                   Instruments (Poly, Bass, rack, params), Metronome, LevelMeter, PreviewQueue
│       ├── dsp/            Biquad, Noise, Oscillator (PolyBLEP), StateVariableLowPass, Envelope (ADSR)
│       ├── midi/           Note model, quantize, MidiFile (Standard MIDI File read/write)
│       ├── project/        Project model (tracks, clips), ProjectFile (JSON), ScheduleBuilder,
│       │                   TakeRecorder (recorded events → notes), MidiExport,
│       │                   Session (single mutation path, undo/redo)
│       ├── util/           LockFreeExchange (audio-thread hand-over), SpscQueue
│       └── time/           MusicalTime (PPQ 960, bar/beat/tick), TempoMap, Transport
├── app/                  BeatForge GUI executable (JUCE)
│   └── Source/
│       ├── Main.cpp        application, main window, smoke-test mode
│       ├── MainComponent.* root content: header, transport, master meter, status
│       ├── audio/          AudioDeviceController (JUCE AudioDeviceManager <-> engine, MIDI inputs)
│       ├── input/          ComputerKeyboard (QWERTY/number rows as a MIDI keyboard)
│       ├── project/        ProjectActions (file commands, dialogs, recent files, Export MIDI)
│       └── ui/             theme, LookAndFeel, TransportBar, ProjectButton, SignatureField, TempoField,
│                           ArrangementView, Selection, DrumMachinePanel, PianoRollPanel, InstrumentPanel, Knob, TabBar,
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

## Instruments (Phase 6)

- **DSP blocks** (`core/dsp`): PolyBLEP saw/square, integrated triangle, sine;
  a TPT state-variable low-pass (stable under per-sample cutoff modulation and
  high resonance; `magnitude()` gives the exact response for the UI curve); a
  linear ADSR whose stage lengths are sample-exact and whose running release
  keeps its slope when parameters are re-applied each block.
- **Nebula Poly:** 16 voices of oscillator → low-pass (opened up to 4 octaves
  by the envelope) → amp ADSR; oldest voice stolen.
- **Nebula Bass:** one voice, held-note stack with last-note priority, legato
  glide (log-frequency), gate-like amp and a filter "pluck" envelope (up to
  5 octaves) that decays to the base cutoff.
- **Parameters:** one table (`ParamSpec`) defines ids, ranges, log/linear
  feel, per-instrument defaults and which knobs an instrument uses; it drives
  validation, the file format and the UI. Values reach the audio thread via
  `ParamBlock` atomics read once per block; cutoff and level are smoothed per
  sample (20 ms); filter coefficients update every 8 samples per voice
  (counted per voice, so output is independent of the buffer size).
- **InstrumentRack:** both instruments exist; the selected one gets new notes.
  Switching releases the old instrument's notes, which ring out.
- **Gain staging:** sources are calibrated so drums (−7.5 dBFS typical),
  four-note chords (−7.3) and bass sum below 0 dBFS at defaults (tested).

## MIDI input and recording (Phase 8)

```
 MIDI device ─(MIDI thread)─► AudioEngine::deviceNote ─┐  SPSC queues, each event
 piano roll keys,             AudioEngine::screenNote ─┘  stamped with now()
 computer keyboard ─(message)─►
                                     │
                     audio thread: place on the audio clock, play on the live
                     target slot (the selected synth track); while recording and
                     playing, report {tick, pitch, velocity} ──► SPSC ──┐
                                                                         ▼
          MainComponent timer (30 Hz) ──► TakeRecorder ──► Session (one undo step)
```

- **Audio clock.** Callbacks can arrive in bursts (PipeWire pulls two 512-sample
  blocks back to back), so the callback time is not the audio time. The engine
  keeps the latest "deadline line" seen (`callback time − samples rendered / rate`;
  a callback can be early but never late for its audio), drifting down 5 µs per
  callback to follow clock drift. Each event plays a fixed delay after it arrived
  (one block + the largest burst seen), so spacing between notes is exact
  (tested with simulated bursts).
- **Where a note is recorded.** Its press time mapped onto the timeline, moved
  back by the output latency (buffer + device latency, set by the app), so it
  lands where the player heard the beat. A press heard just after a loop wrap
  but played for the end of the loop is placed at the loop end.
- **Live voices** have their own id range and survive seeks, loop wraps, clip
  edits and Stop (they belong to the player's hands); note-off, all-notes-off or
  a change of target releases them.
- **TakeRecorder** (message thread): pairs note on/off per pitch; puts a note into
  the clip under its start (overdub), else into this take's clip (grown bar by
  bar while nothing else is in the way), else a new clip (the loop region when
  looping, else whole bars from the note's bar). The take is one Session gesture,
  so one undo step. The engine sends an end-of-take marker when recording or
  playback stops; held keys are closed there.
- **Devices.** `AudioDeviceController` registers one callback for all enabled
  inputs (JUCE serialises them). New inputs (startup or hot-plug via
  `MidiDeviceListConnection`) are enabled once; afterwards the choice in Audio
  Settings is kept in the saved device state.

## MIDI file export (Phase 8)

`exportMidi(Project)` builds a format 1 file at 960 PPQ from what plays:
`buildSchedule` per audible track (same clip cutting and pattern repetition as
playback). Track 1 holds name, tempo and time signature; synth tracks get a GM
program and channels 1–9, 11–16; drums use GM notes on channel 10 (kick 36,
snare 38, closed hat 42, open hat 46, clap 39). `MidiFile` writes/reads SMF
(running status, meta events); the reader verifies exports in tests and is the
base for import later.

## Arrangement (Phase 7)

```
 ArrangementView / editors ──► Session::createClip/moveClip/setTrackVolume/... (undo snapshot)
                                    │
                                    ├─► Project.tracks[i].clips (sorted, absolute ticks)
                                    └─► syncEngine: track i ─► engine slot i (type, mix)
                                                  buildSchedule(track) ─► TrackSchedule
                                                        │  drum hits + note on/off on absolute ticks
                                                        ▼
                         LockFreeExchange<TrackSchedule>: pending ──► current (audio thread, per slot)
```

- **Tracks** are drum or synth (instrument) tracks, at most 16, each mapped to
  one engine slot with its own DrumMachine or InstrumentRack, schedule and
  scratch buffers (all allocated up front). Slots are mixed with smoothed
  volume and balance-pan (10 ms); mute/solo reach exactly zero.
- **Clips** sit at absolute tick positions. `buildSchedule` flattens a track:
  a drum clip repeats its 16-step pattern every bar; notes are cut at the clip
  end; a clip that overlaps a later one stops where the later one starts.
  Voice ids are `clipId << 32 | noteId`, so the same note in two clips is two voices.
- **Seek** moves the play start; Play starts there and Stop returns there.
  A seek or loop jump releases voices whose note does not cover the new position.
- **Undo/redo:** every Session edit stores a snapshot of the whole Project
  (plain data, small) before changing it; 200 steps. `beginGesture`/`endGesture`
  make a whole drag one step. Undo replaces the project and re-syncs the engine.
- **View** (Phase 8): a visible range (start tick + bars) shared by the ruler and
  the lanes; zoom keeps the point under the pointer (or the playhead) in place;
  when the playhead moves out of view the view pages to it. Track order is the
  project's track list (`moveTrack`); engine slots follow track ids, so moving a
  track never interrupts its sound. Track colours follow creation order.
- **Selection** (app) holds the selected track/clip, repairs itself after
  deletes/undo/open, and drives the editors: Step Sequencer for drum clips,
  Piano Roll for synth clips, Instrument for synth tracks.

## MIDI (Phase 5)

```
 PianoRoll ─► Session::addNote/updateNotes/... ─► Project.tracks[i].clips[j].notes
                       │
                       └─► buildSchedule (message thread) ─► TrackSchedule
                                                        │  sorted on/off events, offs first
                                                        ▼
                         LockFreeExchange: pending ──acquire()──► current (audio thread)
                                           retired ◄──────────────┘  freed later by collectGarbage()
```

- **Note** = id, pitch, velocity, start tick, length (clip-local). Quantize
  moves starts to the nearest grid line (ties later), keeps lengths, keeps
  notes inside the clip.
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
- Within a block, note on/off, loop jumps, grid lines (metronome) and drum
  hits are merged in sample order (offs, jumps, grid, drum hits, ons) and voices are rendered
  between them.
- **No stuck notes:** voices are keyed by voice id; on a schedule change, a loop
  wrap, or Stop, voices whose note no longer covers the playhead are released.
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
  (`replaced`, `transport`, `tracks`, `clips`, `instrument`, `metadata`). It
  records undo steps and tracks the file path and the dirty flag (dirty =
  differs from the last saved project, so undoing back to it is clean). Message-thread only. MCP (Phase 10) will call the
  same methods.
- Auditioning a drum is not an edit, so it goes straight to the engine.
- **`ProjectActions`** (app) adds file dialogs, prompts, recent files and
  error reporting on top of the Session. In `--smoke-test` mode it prints
  results instead of opening dialogs and never touches recent-file settings.

### File format v2 (`.beatforge`, UTF-8 JSON, since 0.7)

```json
{ "format": "beatforge-project", "version": 2, "savedBy": "BeatForge 0.7.0",
  "name": "Night Drive", "tempo": 124.0,
  "timeSignature": { "numerator": 4, "denominator": 4 }, "metronome": false,
  "loop": { "enabled": true, "start": 7680, "end": 15360 },
  "tracks": [
    { "type": "drumMachine", "name": "Drums", "volumeDb": 0.0, "pan": 0.0, "muted": false, "soloed": false,
      "lanes": [ { "sound": "kick", "name": "808 Kick", "gainDb": 0.0, "muted": false, "soloed": false } ],
      "clips": [ { "start": 0, "length": 30720,
                   "pattern": { "kick": "x...x...x...x...", "clap": "....x.......x..." } } ] },
    { "type": "instrument", "instrument": "monoBass", "name": "Bass", "volumeDb": -3.0, "pan": 0.0,
      "muted": false, "soloed": false,
      "params": { "polySynth": { "wave": "saw", "cutoff": 2800.0, "...": 0 },
                  "monoBass":  { "wave": "square", "glide": 0.08, "...": 0 } },
      "clips": [ { "start": 15360, "length": 15360,
                   "notes": [ { "pitch": 36, "velocity": 100, "start": 0, "length": 480 } ] } ] } ] }
```

All positions are ticks (960 per quarter note); clip `start` is absolute,
note `start` is relative to its clip. `instrument` is `polySynth` or
`monoBass` (0.5's `basicSynth` loads as `polySynth`); both instruments'
settings are kept so switching back restores the sound.

**Version 1 files (0.4–0.6) are upgraded on open:** the drum pattern becomes
one drum clip at bar 1 (as long as the synth clip, at least one bar), the synth
clip goes on a synth track at bar 1, and the old loop (`startBar`, `bars`)
becomes a loop region. Saving writes version 2.

Loading rules:
- Not JSON, wrong `format`, or missing `version` → refused with a reason.
- `version` newer than this build → refused ("saved by a newer BeatForge"),
  never half-read.
- Out-of-range or wrong-typed values → corrected, each with a warning shown to
  the user; nothing changes on disk until they save.
- Unknown keys (top level, tracks, lanes, clips, notes), extra steps beyond 16,
  unknown lanes and track types, unreadable clips,
  unknown note keys and unreadable notes, unknown instrument settings and
  settings of unknown instruments → kept and written back on save (no silent
  data loss). Out-of-range pitch/velocity/settings are limited with a warning.
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
