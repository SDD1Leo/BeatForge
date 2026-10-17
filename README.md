# BeatForge

A native desktop DAW for electronic music, built in C++ with JUCE.
Small feature set, reliable audio, pleasant workflow.

**Status:** Phase 0 (foundation) — the app launches and shows its window. No audio yet.
See [ROADMAP.md](ROADMAP.md).

## Ubuntu setup (24.04)

```bash
sudo apt install g++ cmake ninja-build pkg-config \
    libasound2-dev libjack-jackd2-dev pipewire-jack \
    libfreetype-dev libfontconfig1-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
    libxinerama-dev libxrandr-dev libxrender-dev libxi-dev \
    libgl1-mesa-dev xvfb
```

`xvfb` is only needed for headless test runs. WebKit, curl and LADSPA are not
needed (those JUCE features are disabled).

## Get the source

```bash
git clone <repo> BeatForge && cd BeatForge
git submodule update --init --depth 1     # JUCE 9.0.4, pinned
```

## Build, test, run

```bash
cmake --preset debug          # configure (fetches Catch2 v3.16.0 on first run)
cmake --build --preset debug  # build
ctest --preset debug          # unit tests + headless launch test
./build/debug/app/BeatForge_artefacts/Debug/BeatForge
```

Use the `release` preset for an optimised build
(`./build/release/app/BeatForge_artefacts/Release/BeatForge`).

### App command-line flags

| Flag | Effect |
|---|---|
| `--version` | Print the version and exit |
| `--smoke-test` | Open the window, confirm it is on screen, quit (exit 0 = OK) |
| `--screenshot=<file.png>` | With `--smoke-test`: save the window content as PNG first |

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) — components and threading rules
- [ROADMAP.md](ROADMAP.md) — phases and their status
- [TESTING.md](TESTING.md) — automated tests and manual checklists
- [CHANGELOG.md](CHANGELOG.md) — what changed per phase
