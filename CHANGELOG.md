# Changelog

## [0.0.1] — Phase 0: foundation (awaiting user test)

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
