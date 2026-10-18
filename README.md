# BeatForge

A native desktop DAW for electronic music, built in C++ with JUCE.
Small feature set, reliable audio, pleasant workflow.

**Status:** Phase 11 (polish and creative workflow) built — awaiting hands-on test. New here? Start with the [user guide](USER_GUIDE.md).
Arrange drum and synth tracks (Nebula Poly or Nebula Bass) as clips on a
timeline, with per-track volume, pan, mute and solo, a loop region, seek and
undo/redo. Play synth tracks from a MIDI keyboard, record takes, and export
the song as a Standard MIDI File or render it to a WAV file. An AI assistant
(Claude Code) can inspect and edit the running project through the MCP server
in `mcp/` — see [mcp/README.md](mcp/README.md). The selected clip opens in the step sequencer or piano roll. Runs on a
sample-accurate musical clock (40–240 BPM, time signatures, metronome) and saves
as versioned `.beatforge` project files.

### Controls

| Action | How |
|---|---|
| Play from a MIDI keyboard | Plug it in (it is enabled automatically; see Audio Settings → Active MIDI inputs) and select a synth track. The status bar shows the inputs and flashes on MIDI |
| Record | Select a synth track, press **Record** or **R**: playback starts and what you play goes into the track (into the clip under the playhead, or a new clip). **R** again stops recording, **Stop**/Space ends it. **Ctrl+Z** removes the whole take |
| Computer keyboard as a MIDI keyboard | Press **K** (or the keyboard button in the transport). **Q W E R T Y U I O P [ ]** are the white keys from C4, **2 3 5 6 7 9 0 =** the black keys; **Z / X** octave down / up, **C / V** velocity down / up. The status bar shows the range and velocity. While it is on, R and Q play notes: record with **Ctrl+R** or the Record button, quantize with its button. **K** again turns it off |
| Record without a keyboard | Turn on the computer keyboard (**K**), or click the piano roll's keyboard while recording |
| Export audio | Project menu → **Export Audio...** or **Ctrl+Shift+E**: choose whole song or loop region, sample rate and bit depth, **Export...**, pick the file; progress and **Cancel** while it renders, then a summary (length, peak, clipping) |
| Export MIDI | Project menu → **Export MIDI...** or **Ctrl+E** (a .mid file next to your project) |
| Add a track | **+ Drums** / **+ Synth** above the track headers (up to 16) |
| Rename / delete a track | Double-click its name / right-click its header → Delete track |
| Reorder tracks | Drag a track header up or down, or right-click → Move up / Move down |
| Zoom / scroll the arrangement | **Ctrl+wheel** or the wheel over the ruler, **-** / **+** below the headers, **Ctrl+=** / **Ctrl+-**; **Fit** shows the whole song; scroll bar or **Shift+wheel** to scroll. The view follows the playhead |
| Track volume / pan / mute / solo | Header slider (double-click = 0 dB), pan knob, **M**, **S** |
| Create a clip | Double-click an empty spot in a track's lane (a 1-bar pattern on drum tracks, 4 bars on synth tracks) |
| Select / open a clip | Click it / double-click it (opens it in the editor below) |
| Move a clip | Drag it (snaps to beats; drop on another track of the same kind to move it there) |
| Resize a clip | Drag its right edge |
| Duplicate / delete a clip | **Ctrl+D** / **Delete**, or right-click it |
| Loop region | Drag across the ruler's thin top strip (snaps to bar lines; a click loops one bar). Loop button or **L** turns looping on/off |
| Move the playhead | Click or drag the ruler below the strip. Play starts there; Stop returns there |
| Undo / redo | **Ctrl+Z** / **Ctrl+Shift+Z** or **Ctrl+Y**, also in the project menu |
| Switch editor / instrument / mixer | Tabs below the arrangement, or **Ctrl+1** / **Ctrl+2** / **Ctrl+3**. The editor follows the selected track: Step Sequencer for drums, Piano Roll for synths |
| Mix | **Mixer** tab: per track fader, pan, mute/solo and level meter; master volume on the right |
| Instrument presets | Instrument tab → preset menu (Pluck, Soft Pad, Bright Lead, Keys, Warm Sine; Sub, Acid, Punchy, Smooth Saw) |
| Copy / cut / paste a clip | **Ctrl+C** / **Ctrl+X**; select a track, click the ruler where it should go, **Ctrl+V** |
| All shortcuts | **F1** |
| User guide | Project menu → Help → **User Guide** |
| Build a beat | Click steps in the step grid; click-drag along a row to paint several. While stopped, a newly set step is auditioned. The pattern repeats every bar of the clip |
| Hear a drum | Click its name |
| Drum level / mute / solo | Row slider (double-click = 0 dB), **M**, **S** buttons |
| Clear the pattern | **Clear** |
| Rename a drum | Double-click its name |
| Project menu | Click the project name (top left): New, Open, Open Recent, Save, Save As, Rename, Undo, Redo, Export Audio, Export MIDI |
| New / Open / Save / Save As | **Ctrl+N** / **Ctrl+O** (project browser) / **Ctrl+S** / **Ctrl+Shift+S**. Unsaved work is autosaved every minute and offered back after a crash |
| Choose a synth track's instrument | Select the track → Instrument tab → **Nebula Poly** (chords, leads) or **Nebula Bass** (mono, glide) |
| Shape the sound | Instrument tab: wave buttons; knobs (drag up/down or scroll, Shift = fine, double-click = default); **Reset** restores defaults |
| Clip length | Drag the clip's edge, or the bars dropdown in the piano roll header |
| Draw / select notes | Click empty grid to add a note; drag on empty grid to box-select; Shift/Ctrl+click adds to the selection |
| Move / resize / delete notes | Drag a note; drag its right edge; right-click it or press **Delete** |
| Velocity | Drag the bars in the velocity lane (selected notes move together) |
| Transpose / nudge | **Up/Down** (Shift = octave), **Left/Right** by the grid |
| Quantize | **Q** or the Quantize button (selection, or all notes); grid 1/4…1/32 in the header |
| Hear a pitch | Click the piano keyboard |
| Play / Stop | Play/Stop buttons, or **Space** |
| Tempo | Drag the BPM field up/down or scroll (Shift = 0.1 BPM), double-click to type |
| Time signature | Click **SIG** in the readout (2/4 … 7/8) |
| Metronome (off by default) | Metronome button or **M** |

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
| `--export-midi=<file.mid>` | Export the (opened) project as a MIDI file at launch |
| `--export-wav=<file.wav>` | Render the (opened) project to WAV at launch; options `--wav-rate=44100\|48000\|96000`, `--wav-bits=16\|24\|32`, `--wav-range=loop` |

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

## AI assistant (MCP)

```bash
cd mcp && npm ci && npm run build
claude mcp add --scope user beatforge -- node ~/e_drive/BeatForge/mcp/dist/index.js
```

Start BeatForge (status bar: **MCP ready**), start a new Claude Code session and
ask, e.g., "set the tempo to 128 and add a bass line in BeatForge". Edits show
up in the app at once and can be undone with Ctrl+Z. Files are only read and
written in `~/Music/BeatForge`; destructive steps need your confirmation.
Details: [mcp/README.md](mcp/README.md).

## Documentation

- [USER_GUIDE.md](USER_GUIDE.md) — make your first track, step by step
- [ARCHITECTURE.md](ARCHITECTURE.md) — components and threading rules
- [DESIGN.md](DESIGN.md) — NEBULA visual design system (tokens, conventions, redesign plan)
- [ROADMAP.md](ROADMAP.md) — phases and their status
- [TESTING.md](TESTING.md) — automated tests and manual checklists
- [CHANGELOG.md](CHANGELOG.md) — what changed per phase
