# BeatForge

A native desktop DAW for electronic music, built in C++ with JUCE.
Small feature set, reliable audio, pleasant workflow.

**Status:** Phase 1 (audio output & transport) built — awaiting hands-on test.
BeatForge opens your audio device and plays a test tone from a transport bar,
with an output level meter.
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
| `--smoke-play` | Like `--smoke-test`, plus: press Play, check the audio clock keeps real time for 2 s, press Stop, check it stops. **Plays the tone on your speakers.** |
| `--screenshot=<file.png>` | Save the window content as PNG (mid-playback with `--smoke-play`) |

## Audio on Linux

BeatForge uses **ALSA**. On Ubuntu 24.04 the ALSA "default" device is PipeWire,
so audio follows your system output (speakers, HDMI, Bluetooth). Pick a
different device, sample rate or buffer size in **Audio Settings...**; the
choice is saved in `~/.config/BeatForge/BeatForge.settings`.

JACK support is compiled out for now (see ROADMAP known limitations).

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) — components and threading rules
- [ROADMAP.md](ROADMAP.md) — phases and their status
- [TESTING.md](TESTING.md) — automated tests and manual checklists
- [CHANGELOG.md](CHANGELOG.md) — what changed per phase
