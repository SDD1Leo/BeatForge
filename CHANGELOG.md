# Changelog

## [0.6.0] — Phase 6: built-in synthesizer and bass (approved 2026-10-18)

### Added
- Nebula Poly: 16-voice subtractive synth (sine/saw/square/triangle, resonant
  low-pass with envelope amount, ADSR, level).
- Nebula Bass: monophonic bass with last-note priority, legato glide and a
  filter pluck envelope.
- DSP: PolyBLEP oscillators, TPT state-variable low-pass, sample-exact ADSR.
- Instrument tab (Ctrl+3): instrument choice, OSCILLATOR / FILTER / ENVELOPE
  sections with Nebula knobs and live waveform, filter-response and envelope
  displays; Reset to defaults.
- Reusable Nebula rotary knob (design step 3, part 1).
- Instrument and both instruments' settings saved in projects.
- 15 new unit tests.

### Changed
- Gain staging: drum kit 6 dB quieter, Poly default −10 dB, Bass default
  −6 dB, so drums + chords + bass stay below 0 dBFS at defaults.
- 0.5 projects' `basicSynth` track now plays with Nebula Poly.
- The synth track is always saved (its settings matter before a clip exists).

### Fixed
- Envelope releases stretched when parameters were re-applied each block.

## [0.5.0] — Phase 5: MIDI engine and piano roll (approved 2026-10-18)

### Added
- MIDI note/clip model, quantize (1/4…1/32), note constraints.
- Sample-exact note scheduling from transport segments; lock-free schedule
  hand-over; no stuck notes on edit, Stop or loop wrap.
- Basic 16-voice synth (PolyBLEP saw, low-pass, ADSR, velocity).
- Synth track with a looping MIDI clip (1/2/4/8 bars) saved in projects;
  unknown/invalid note data preserved.
- Session note editing (add, update, remove, quantize) as the single path.
- NEBULA piano roll: piano keyboard (click to hear), grid with bar/beat/grid
  lines, draw / box-select / move / resize / delete, keyboard editing
  (Delete, Ctrl+A, arrows, Q), velocity lane, playhead, note count.
- Workspace tabs Drums | Piano Roll (Ctrl+1 / Ctrl+2).
- 24 new unit tests incl. randomized tempo-change stress test.

### Fixed
- One-letter shortcuts (M, L, Q) no longer fire with Ctrl held (Ctrl+Q quits).

## [0.4.0] — Phase 4: project saving and loading + Nebula transport (approved 2026-10-18)

### Added
- `.beatforge` project files: versioned JSON (format v1) with tempo, time
  signature, metronome, loop, drum pattern and lane names/volume/mute/solo.
- Safe loading: invalid files refused with reasons, newer versions refused,
  out-of-range values corrected with listed warnings, unknown data preserved.
- Atomic saving (temp file + rename).
- `Session`: the single path for project edits (GUI now, MCP later).
- Project menu: New, Open, Open Recent, Save, Save As, Rename; Ctrl+N/O/S,
  Ctrl+Shift+S; unsaved-changes prompt on New/Open/Quit; `*` in the title.
- Rename drum lanes by double-clicking; an untitled project takes its file name.
- Open a project from the command line.
- NEBULA transport (design step 2): project button + Saved/Unsaved state,
  captioned readout strip BPM | SIG | BAR.BEAT.TICK | TIME, gear button for
  audio settings; Nebula alert dialogs and file browser.
- 17 unit tests and 4 app-level tests; project fixtures in `tests/fixtures`.

### Changed
- Settings moved from `~/BeatForge/` to `~/.config/BeatForge/` (migrated on
  first launch).
- Minimum window width 1080 px (TIME readout hides below 1180 px).
- Keyboard shortcuts are handled at window level (work right after dialogs).

### Fixed
- Smoke tests no longer write recent files into the user's settings.
- Save errors include the operating-system reason.

## [Unreleased] — NEBULA visual system, step 1

### Changed
- New canonical visual direction NEBULA (see DESIGN.md, docs/design/).
- Theme tokens rewritten (midnight surfaces, violet/magenta/cyan signal
  colours, semantic mute/solo/meter/playhead colours, fixed track colours,
  Ubuntu Sans / Ubuntu Sans Mono typography).
- Central LookAndFeel now styles buttons, combo boxes, linear sliders, popup
  menus, tooltips and the Audio Settings dialog.
- Existing screens re-skinned: waveform wordmark, violet Play/Loop/Metronome
  when on, magenta downbeat / cyan beat lights, violet loop band and playhead,
  track-colour stripes on drum lanes, cyan→violet→magenta meter.
- No layout, behaviour or audio changes (verified: identical timing and levels).

## [0.3.0] — Phase 3: first drum machine (approved 2026-10-18)

### Added
- 16-step, 5-lane drum machine: kick, snare, closed hat, open hat, clap.
- Built-in kit synthesised deterministically into sample buffers at startup.
- Sample-accurate step triggering on the 16th-note grid; pattern loops every
  bar in sync with tempo changes.
- Per-drum volume (−60…+6 dB, 5 ms click-free ramps), mute, solo; closed hat
  chokes open hat; click-free retriggers.
- Drum grid UI: click or drag-paint steps, audition on click while stopped,
  click a drum name to hear it, playing-step highlight, trigger lights, Clear.
- `Biquad` filter and deterministic `Noise` DSP blocks.
- `tools/analyze_onsets.py --rise` for overlapping hits.
- 18 new unit tests (kit, drums, controls, step grid).

### Changed
- Transport emits 16th-note grid lines (beats flagged) instead of beats only.
- Stop rewinds immediately; tails ring out (no more "stopping" state).
- Metronome is off by default (M toggles it).
- Ruler pages with the playhead past bar 8.

### Fixed
- Default kit levels clipped when several drums hit together.
- Lane gain ramps no longer depend on the audio buffer size.

## [0.2.0] — Phase 2: tempo engine & metronome (approved 2026-10-18)

### Added
- Musical time core: 960 PPQ ticks, time signatures, bar.beat.tick conversion.
- `TempoMap`: piecewise tempo, ticks ↔ seconds ↔ samples (multi-tempo ready).
- `Transport`: drift-free audio-thread clock with sample-accurate beat events,
  live tempo changes, loop regions.
- Metronome click voice (accented downbeat), rendered sample-accurately.
- UI: tempo field (drag/scroll/type, 40–240), time-signature menu (2/4…7/8),
  metronome toggle (M), loop toggle (L) + length (1/2/4/8 bars), bar.beat.tick
  + clock display, beat lights, bar ruler with loop region and playhead.
- `tools/analyze_onsets.py`; virtual-sink + Xvfb/xdotool test recipes.
- 22 new unit tests (time, tempo, transport, loop, metronome, engine).

### Changed
- The metronome replaces the Phase 1 test tone as the sound source (Audio
  Settings keeps JUCE's own device Test button).
- `--smoke-play` waits 1 s before Play and checks the beat count.

### Fixed
- Loop end landing exactly on an audio block boundary did not wrap (caught by
  tests before release).

## [0.1.0] — Phase 1: audio output & transport (approved 2026-10-18)

### Added
- `AudioEngine` (core, pure C++): lock-free play/stop, transport state and
  sample position, allocation-free process callback.
- 440 Hz / -18 dBFS test tone with a 10 ms click-free ramp.
- Lock-free stereo peak meter; UI meter with decay, peak hold and clip lights.
- `AudioDeviceController`: opens the saved or default device (ALSA → PipeWire),
  persists device settings, reports device errors and dropouts.
- Transport bar: Play, Stop, time display, state pill, Audio Settings dialog
  (device / sample rate / buffer size). Space toggles play/stop.
- Status bar shows device, rate, buffer, latency and dropouts; turns red with
  a reason when no device is available (Play is disabled then).
- `--smoke-play` real-device check; `--screenshot` now works mid-playback.
- 15 new unit tests incl. a zero-allocation test for the audio callback path.

### Fixed
- Meter gradient stops were outside 0..1 (JUCE assertion).
- Allocation-counter self-test was optimised away in Release builds.

### Changed
- JUCE JACK backend disabled (noisy without a JACK server); ALSA only.

## [0.0.1] — Phase 0: foundation (approved 2026-10-17)

### Added
- CMake + Ninja build with `debug`/`release` presets; C++20.
- JUCE 9.0.4 as a pinned git submodule.
- `beatforge_core` GUI-free library (build/version info for now).
- BeatForge desktop app: dark themed main window, header, status bar,
  close-button and Ctrl+Q shutdown.
- `--version`, `--smoke-test`, `--screenshot=<png>` flags.
- Catch2 unit tests and a headless app launch test (fails on JUCE assertions).
- README, ARCHITECTURE, ROADMAP, TESTING, CHANGELOG.

### Fixed
- Non-ASCII literal in the status bar triggered a JUCE assertion
  (`juce_String.cpp:314`); now built with `String::fromUTF8`.
