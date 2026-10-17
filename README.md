# BeatForge

A native desktop DAW for electronic music, built in C++ with JUCE.
Small feature set, reliable audio, pleasant workflow.

**Status:** Phase 6 (built-in synthesizer and bass) built — awaiting hands-on test.
A 16-step drum machine and a Synth track (Nebula Poly or Nebula Bass) playing
a looping MIDI clip edited in a piano roll, on a sample-accurate musical clock
(40–240 BPM, time signatures, loop, metronome), saved as versioned
`.beatforge` project files.

### Controls

| Action | How |
|---|---|
| Build a beat | Click steps in the drum grid; click-drag along a row to paint several. While stopped, a newly set step is auditioned |
| Hear a drum | Click its name |
| Drum level / mute / solo | Row slider (double-click = 0 dB), **M**, **S** buttons |
| Clear the pattern | **Clear** |
| Rename a drum | Double-click its name |
| Project menu | Click the project name (top left): New, Open, Open Recent, Save, Save As, Rename |
| New / Open / Save / Save As | **Ctrl+N** / **Ctrl+O** / **Ctrl+S** / **Ctrl+Shift+S** |
| Switch Drums / Piano Roll / Instrument | Tabs above the editor, or **Ctrl+1** / **Ctrl+2** / **Ctrl+3** |
| Choose the synth track's instrument | Instrument tab → **Nebula Poly** (chords, leads) or **Nebula Bass** (mono, glide) |
| Shape the sound | Instrument tab: wave buttons; knobs (drag up/down or scroll, Shift = fine, double-click = default); **Reset** restores defaults |
| Create a MIDI clip | Piano Roll tab → **Create 4-bar MIDI clip**; length 1/2/4/8 bars in the header |
| Draw / select notes | Click empty grid to add a note; drag on empty grid to box-select; Shift/Ctrl+click adds to the selection |
| Move / resize / delete | Drag a note; drag its right edge; right-click it or press **Delete** |
| Velocity | Drag the bars in the velocity lane (selected notes move together) |
| Transpose / nudge | **Up/Down** (Shift = octave), **Left/Right** by the grid |
| Quantize | **Q** or the Quantize button (selection, or all notes); grid 1/4…1/32 in the header |
| Hear a pitch | Click the piano keyboard |
| Play / Stop | Play/Stop buttons, or **Space** |
| Tempo | Drag the BPM field up/down or scroll (Shift = 0.1 BPM), double-click to type |
| Time signature | Click **SIG** in the readout (2/4 … 7/8) |
| Metronome (off by default) | Metronome button or **M** |
| Loop | Loop button or **L**; length (1/2/4/8 bars from bar 1) in the dropdown |
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
| `--smoke-play` | Like `--smoke-test`, plus: wait 1 s, press Play, check the audio clock keeps real time for 2 s and every metronome beat fired, press Stop, check it stops. **Plays clicks on your output** (see TESTING.md to route it to a virtual sink). |
| `--screenshot=<file.png>` | Save the window content as PNG (mid-playback with `--smoke-play`) |
| `<project>.beatforge` | Open that project at launch |

## Audio on Linux

BeatForge uses **ALSA**. On Ubuntu 24.04 the ALSA "default" device is PipeWire,
so audio follows your system output (speakers, HDMI, Bluetooth). Pick a
different device, sample rate or buffer size in **Audio Settings...**; the
choice is saved in `~/.config/BeatForge/BeatForge.settings` (versions before
0.4 used `~/BeatForge/`; the file is moved automatically).

## Projects

Projects are `.beatforge` files: readable, versioned JSON (see
ARCHITECTURE.md). New projects are saved to `~/Music/BeatForge/` by default.
Saving writes a temporary file and renames it, so a failed save never damages
the previous file. BeatForge asks before discarding unsaved changes (New,
Open, Quit); the title shows `*` and the transport shows **Unsaved** until you
save.

JACK support is compiled out for now (see ROADMAP known limitations).

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) — components and threading rules
- [DESIGN.md](DESIGN.md) — NEBULA visual design system (tokens, conventions, redesign plan)
- [ROADMAP.md](ROADMAP.md) — phases and their status
- [TESTING.md](TESTING.md) — automated tests and manual checklists
- [CHANGELOG.md](CHANGELOG.md) — what changed per phase
