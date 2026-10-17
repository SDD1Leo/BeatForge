# Changelog

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
