# Testing BeatForge

```bash
ctest --preset debug
```

## Automated tests

| Test | What it proves | Needs |
|---|---|---|
| `[buildinfo]` unit tests | Version metadata wired correctly from CMake; JUCE pinned to 9.0.4 | nothing |
| `[time]` | PPQ 960, ticks per beat/bar for 2/2…7/8, bar.beat.tick conversion incl. negatives, round-trips | nothing |
| `[tempo]` | Tick↔seconds↔samples at 40–240 BPM × 44.1/48/96 kHz, known values (120 BPM @ 48 kHz = 25 samples/tick), piecewise tempo changes, validation | nothing |
| `[transport]` | Beats on exact samples (120 BPM @ 48k → every 24000; 100 @ 44.1k → 26460), identical for block sizes 1–4096, **no drift over 1 hour** at 120/133/97.3 BPM, accents per time signature, tempo change keeps the grid, 300+ tempo changes never duplicate/skip a beat | nothing |
| `[loop]` | Wrap re-fires the loop's first beat on the exact sample, loops not starting at 0, block size 1, invalid loops ignored | nothing |
| `[metronome]` | Click length/decay/silence after, accent louder and higher, identical when rendered in pieces | nothing |
| `[engine]` | Silence when stopped, clicks heard exactly on beats, tempo from another thread, muted metronome keeps the clock, stop lets the click ring then rewinds, loop in engine, 500 play/stop cycles, device restart at a new rate | nothing |
| `[meter]` | Peak accumulation/reset, dB conversion, meter sees the clicks | nothing |
| `[drumkit]` | All five sounds at 44.1/48/96 kHz: sane length, finite, peak in range, end exactly at 0, attack within 2 ms; deterministic; kick low / hats bright; **all five on one step stay < 0 dBFS** | nothing |
| `[drums]` | Kicks on exact step samples, four-on-the-floor trigger count, 16th hats every 6000 samples @ 120/48k, clap backbeat at 100 BPM over 3 bars, **bit-identical output for block sizes 1/37/64/256/1000**, live step edits, audition while stopped, closed hat chokes open hat, stop lets tails ring with no new hits | nothing |
| `[controls]` | Mute, solo, -6 dB halves level, minimum volume = silence, mute while ringing is silent after the 5 ms ramp with 64- and 4096-sample buffers | nothing |
| `[steps]` | Every 16th is a grid line with exact samples; 6/8 beat flags | nothing |
| `[project][file]` | JSON round trip, readable step strings, save/load file, atomic save leaves the old file intact on failure, missing folder error, non-project files refused with reasons, newer version refused, minimal v1 file, out-of-range values corrected with 5 warnings, **unknown data survives two load/save cycles**, optional fields silent | temp dir |
| `[session]` | Edits update project + engine + dirty flag + listeners, no-op edits stay clean, loop length follows the time signature, rename validation, open replaces engine state, **save → reopen sounds bit-identical** | nothing |
| `[midi][quantize]` | Nearest-line rounding, ties later, clip-end clamp, all grids, selection-only | nothing |
| `[midi][schedule]` | Event order (offs first), clip-end offs wrap to tick 0, notes beyond the clip skipped, lock-free exchange hand-over/garbage, zero allocations on acquire | nothing |
| `[midi][synth]` | A4 = 440 Hz, level bounds, velocity scaling, release to silence, same-pitch overlap released by id, voice stealing | nothing |
| `[midi][engine]` | Note-on and note-off on exact samples, **bit-identical for block sizes 1/37/256/1000**, clip loops, tempo change keeps notes on the grid, **random tempo changes at block sizes 1/37/64/4096 bit-identical**, auditions while stopped, zero allocations with clip swaps | nothing |
| `[stuck]` | Deleting or moving a sounding note releases it; Stop releases all; a loop wrap releases notes crossing the loop end | nothing |
| `[session][midi]` | Clip creation, add/update/quantize/remove notes, notes kept beyond a shortened clip, clip+notes survive save/load, bad notes corrected or kept verbatim | nothing |
| `app_open_*` | Real app opens fixture files: valid demo (0 warnings), corrected tempo (1 warning listed), newer format refused, truncated JSON refused | X server |
| `[realtime]` | **Callback path makes zero heap allocations** (global `operator new` counter), plus a self-check that the counter works | nothing |
| `app_smoke_launch` | Real GUI app starts, window reaches the screen, app quits cleanly. **Fails on any JUCE assertion or leaked object.** | X server (uses `xvfb-run` if installed) |

### Real-device checks without disturbing your speakers

Route BeatForge to a temporary virtual PipeWire sink, record that sink, and
analyse the result. This proves the audio reached the sound server with the
right timing; it does not prove your speakers work.

```bash
# 1. temporary virtual sink
pw-cli create-node adapter '{ factory.name=support.null-audio-sink node.name=beatforge-test media.class=Audio/Sink object.linger=true audio.position=[FL FR] audio.rate=48000 }'
# 2. record it, run the app against it
pw-record --target beatforge-test -P '{ stream.capture.sink=true }' --rate 48000 --channels 2 --format f32 /tmp/cap.wav &
PIPEWIRE_NODE=beatforge-test ./build/debug/app/BeatForge_artefacts/Debug/BeatForge --smoke-play; kill %1
# 3. analyse: expect 0.5000 s intervals at 120 BPM, downbeats ~4.5 dB louder
python3 tools/analyze_onsets.py /tmp/cap.wav
#    hits that overlap the previous tail (e.g. a kick every beat): add --rise
python3 tools/analyze_onsets.py /tmp/cap.wav --rise
# 4. remove the sink (id from `pw-cli ls Node | grep -B3 beatforge-test`)
pw-cli destroy <id>
```

Recording the default sink monitor instead also captures any other app's audio
(browsers etc.), so prefer the virtual sink.

### Isolating UI tests from your files

Run the app with a scratch home so test projects, recent files and settings
never land in your real `~/Music` or `~/.config`:

```bash
HOME=/tmp/bf-home PIPEWIRE_NODE=beatforge-test ./build/debug/app/BeatForge_artefacts/Debug/BeatForge
```

### Driving the real UI headlessly

`Xvfb :99` + `xdotool` (installed) can click buttons, drag the tempo, pick
menu items and press keys in the real app while audio goes to the virtual sink;
`ffmpeg -f x11grab -i :99 -frames:v 1 shot.png` captures the screen. Xvfb has no
window manager, so after a dropdown closes run `xdotool windowfocus --sync <win>`
before sending keys (a real desktop does this itself). Send keys as real input
(`xdotool key ctrl+s`, no `--window`): synthetic `--window` events are
ignored by the app once focus has moved through a dialog.

Simulate "no audio device": `ALSA_CONFIG_PATH=/path/to/empty-file BeatForge`.

Proof levels we keep separate (from Phase 1 on):

1. **Code behaviour** — unit tests on pure logic (conversions, scheduling).
2. **Generated audio** — render to buffers/files and analyse samples.
3. **Real device** — only a human (or a run on real hardware) proves sound
   actually comes out of the speakers. Automated tests never claim this.

## Manual checklist — Phase 5

1. Press **Ctrl+2** (or the Piano Roll tab) → **Create 4-bar MIDI clip**.
2. Pick grid **1/8**, click to draw a short melody (each click plays the note).
3. Drag one note to a new time/pitch; drag another note's right edge to make it longer.
4. In the velocity lane drag one bar down — that note gets quieter.
5. Press **Space** — the melody loops with your drums; the playhead moves through the piano roll.
6. Draw a note with grid 1/32 off the beat, switch to 1/4, select it, press **Q** — it snaps.
7. Save (Ctrl+S), reopen — the melody comes back.

## Manual checklist — Phase 4

1. Build a beat, set the tempo to e.g. 124, rename a drum (double-click its name).
2. **Ctrl+S** — the save dialog suggests `~/Music/BeatForge/Untitled.beatforge`; name it and save. The project takes the file's name; the transport says **Saved**.
3. Close BeatForge. Reopen it, click the project name → **Open Recent** → your project.
4. Press **Space** — the same beat, tempo, names and mute/solo come back.
5. Change a step, then try to quit — BeatForge asks **Save changes?**; Cancel keeps it open.
6. Optional: open `tests/fixtures/newer-version.beatforge` or `truncated.beatforge` — a clear error, nothing breaks.

## Manual checklist — Phase 3

1. Launch. Click **Kick** steps 1, 5, 9, 13 — each click plays the kick.
2. Click **Clap** steps 5 and 13. Drag along the **Closed Hat** row to fill it.
3. Press **Space** — the beat loops; the playing step column lights; drum names flash on hits.
4. Change the tempo while it plays (type 100, then drag up) — the beat stays tight.
5. Try **M** / **S** on a lane and its volume slider — changes are immediate and click-free.
6. Add snare on 5 and 13 and open hats on 3, 7, 11, 15 — the master meter's clip lights stay dark.
7. **Clear** — the grid empties; playback goes silent.

## Manual checklist — Phase 2

1. Launch. Press **Space** — metronome clicks at 120 BPM, downbeat higher/louder, display counts `1.1.000 → 1.2.000 …`, beat lights step.
2. Double-click the BPM field, type **100**, Enter — clicks slow down. Then drag it to **120** — clicks speed up smoothly.
3. Let it run **≥ 1 minute** — steady, no stumbles; display time matches a clock.
4. Choose **3/4** — 3 beat lights, accent every 3rd click. Back to **4/4**.
5. Press **L** (loop, 4 bars) — playhead jumps back to bar 1 after bar 4 with no hiccup in the clicks. Try **1 bar**.
6. Press **M** — clicks stop, clock keeps running; **M** again — clicks return.
7. After using a dropdown, press **Space** — it still stops/starts playback.

## Manual checklist — Phase 1

1. Launch: `./build/debug/app/BeatForge_artefacts/Debug/BeatForge`. Status bar is green and names your device.
2. Click **Play** — you hear a steady 440 Hz tone; clock runs; pill says PLAYING; both meters sit at -18.
3. Click **Stop** — tone ends without a click; clock returns to 00:00.000.
4. Press **Space** a few times — toggles play/stop each time.
5. **Audio Settings...** — change buffer size (e.g. 256), close; status bar updates; Play still works.
6. While playing, switch output device (e.g. speakers ↔ earbuds) — playback stops, error-free; Play again works.

## Manual checklist — Phase 0

1. `./build/debug/app/BeatForge_artefacts/Debug/BeatForge`
2. A dark "BeatForge" window opens (header, "Foundation ready" card, status bar).
3. Resize it — layout follows; it won't shrink below 960×600.
4. Close with the window's ✕ — terminal prints `BeatForge shut down cleanly`.
5. Relaunch, press **Ctrl+Q** — same clean exit.

## Results log

| Date | Phase | Result |
|---|---|---|
| 2026-10-18 | 5 | 103/103 ctest pass in Debug and Release (24 new). Real UI (Xvfb, sandboxed HOME, virtual sink): created a 4-bar clip, set 1/8, clicked an 8-note melody (8 auditions heard), dragged C5 → one eighth later + D5, stretched F4 to 3 eighths, dragged a velocity bar; playback analysed note by note: onsets **0 / 0.5 / 1.0 / 1.75 / 2.0 / 2.5 / 3.0 / 3.5 s**, pitches **C4 E4 G4 D5 B4 G4 A4 F4**, quieter C4 (−5.8 dB), F4 sounding through 4.05 s; off-grid note quantized with Q; Delete, Ctrl+A, Up/Shift+Up work; saved and reopened from the command line with all notes. Found & fixed: notes fired one sample early at some block sizes (event window now sample-exact), loop-wrap release missed when a wrap ended a block, piano-roll **Ctrl+Q quantized instead of quitting** (and Ctrl+M/L toggled), keyboard drawing. User hands-on test: approved 2026-10-18. |
| 2026-10-18 | 4 | 79/79 ctest pass in Debug and Release (21 new: file format, session, 4 app-level open tests). Isolated real-UI run (Xvfb, `HOME` sandboxed, virtual sink): built a 124 BPM beat by mouse, renamed Kick → "808 Kick", muted Snare, **Ctrl+S** via the JUCE browser → project named "Night Drive", **Saved**, readable JSON on disk; quit (no prompt); relaunched → **Open Recent** → identical state, playback **0.4839 s** intervals before and after (124 BPM); edit → quit prompt → **Cancel** keeps app open; **Ctrl+S** wrote directly; Ctrl+Shift+S, Escape, Space all work with real key input; clean exit, 0 assertions. Found & fixed: alert icon clipped; tests wrote recent files into real settings; settings were in `~/BeatForge` (moved to `~/.config/BeatForge`, migrated); zenity 4.0.1 ignored the suggested file name (switched to JUCE's themed browser); shortcuts made window-level. User hands-on test: approved 2026-10-18. |
| 2026-10-18 | Nebula 1 | 58/58 ctest pass. Xvfb UI session with the restyled app: house beat entered by mouse, loop/solo/mute toggled, captured mix identical to pre-restyle (**0.5000 s** grid, peaks **-1.5 / -5.4 dBFS**); screenshots of main view, dropdown menu, tooltip and Audio Settings dialog all themed; clean shutdown, zero JUCE assertions. No hard-coded colours outside Theme.h. |
| 2026-10-18 | 3 | 58/58 ctest pass in Debug and Release. Real UI driven in Xvfb (audio to a virtual PipeWire sink): clicking steps while stopped auditioned 6 hits; drag-painted 16 hats; played the user-checklist beat — solo captures: kick **0.5000 s**, clap **1.0000 s**, hats **0.1250 s** @ 120 BPM; typed 100 BPM mid-playback → **0.6000 s**; muting the kick left hats at **0.1500 s**. Found & fixed: default kit clipped (+3.2 dBFS on kick+hat+clap; red clip lights on kick+snare) → kit rebalanced, busy house beat now peaks **-1.5 dBFS, 0 clipped**; ruler lost the playhead after bar 8 → pages now; dangling-reference crash in a test. `--smoke-play` on real device OK. User hands-on test: approved 2026-10-18. |
| 2026-10-18 | 2 | 40/40 ctest pass in Debug and Release (178k assertions incl. a 1-hour no-drift run). `--smoke-play` on real device: 2.01 s audio / 2.01 s wall, 5 beats @ 120, 0 dropouts. Virtual-sink capture: click intervals **0.5000 s** @ 120 BPM, downbeat -6.3 dBFS vs -10.8 dBFS. Real UI driven in Xvfb with xdotool: typed 100 BPM → **0.6000 s**; dragged tempo during playback → clean transition to 143 BPM (**0.4196 s**), no doubled/missing click; 3/4 + 1-bar loop → constant 0.4196 s across loop wraps, accent every 3rd click; Space/M/L keys, Ctrl+Q clean exit, zero JUCE assertions. User hands-on test on speakers: approved 2026-10-18. |
| 2026-10-17 | 1 | 19/19 ctest pass in Debug and Release. On real hardware (`:1`, ALSA → PipeWire → AirBass Earbuds BT sink, 44.1 kHz/512): `--smoke-play` rendered 2.02 s of audio in 2.02 s wall (ratio ~1.00), 0 dropouts, stopped cleanly. Sink-monitor capture: 440 Hz at **-18.0 dBFS** for ~2.1 s, silence after Stop. No-device path (broken ALSA config): red status, Play disabled, no crash. User hands-on test on speakers: all checks pass — approved 2026-10-18. |
| 2026-10-17 | 0 | 3/3 automated tests pass (Debug, Xvfb). Launched on real X11 display `:1`, window 1280×800, clean shutdown. User hands-on test: all 5 checks pass — approved. |
