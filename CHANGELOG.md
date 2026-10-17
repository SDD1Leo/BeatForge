# Changelog

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
