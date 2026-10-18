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
| `[project][file]` | JSON round trip (v2: tracks, clips, step strings), save/load file, atomic save leaves the old file intact on failure, missing folder error, non-project files refused with reasons, newer version refused, **v1 files upgraded** (pattern → drum clip, synth clip → synth track, loop kept), out-of-range values corrected with warnings, **unknown data survives two load/save cycles** | temp dir |
| `[session]` | Edits update project + engine + dirty flag + listeners, no-op/invalid edits ignored, tracks add/rename/mix/remove (16 max), clips create/move (same kind only)/resize/duplicate/delete, notes per clip, **undo/redo restore project and sound**, a drag is one undo step, undo restores a deleted track with clips, open clears history, **save → reopen sounds bit-identical** | nothing |
| `[arrangement]` | Clips sound exactly where they sit (drum clip at bar 3 first hits on its sample; notes cut at clip end), **mute/solo exact silence, −6 dB halves, pan left/right** per track, empty slot silent, seek + Stop returns there, seeking releases notes, **slot type/mix/schedule changes allocation-free** with 8192-sample blocks | nothing |
| `[midi][quantize]` | Nearest-line rounding, ties later, clip-end clamp, all grids, selection-only | nothing |
| `[midi][schedule]` | Event order (offs first), notes cut at clip ends, a later clip takes over from an overlapping one, drum clips repeat their pattern every bar, lock-free exchange hand-over/garbage, zero allocations on acquire | nothing |
| `[midi][synth]` | A4 = 440 Hz, level bounds, velocity scaling, release to silence, same-pitch overlap released by id, voice stealing | nothing |
| `[midi][engine]` | Note-on and note-off on exact samples, **bit-identical for block sizes 1/37/256/1000**, clip loops, tempo change keeps notes on the grid, **random tempo changes at block sizes 1/37/64/4096 bit-identical**, auditions while stopped, zero allocations with clip swaps | nothing |
| `[stuck]` | Deleting or moving a sounding note releases it; Stop releases all; a loop wrap releases notes crossing the loop end | nothing |
| `[dsp]` | Every waveform at 110/440/1000 Hz in range, low-pass passband/stopband/resonance matching `magnitude()`, stable under per-sample sweeps at full resonance, ADSR sample-exact (incl. release not stretched by parameter updates), retrigger from current level | nothing |
| `[instrument]` | Poly: cutoff/level/wave/attack audibly change output, filter envelope opens then closes; Bass: mono, last-note priority, glide halfway point exact, no glide from silence; switching instruments releases old notes; engine cutoff changes the sound; parameter changes and switching allocation-free; **chords + bass + full beat < 0 dBFS at defaults** | nothing |
| `[session][instrument]` | Choice/settings through the session (clamping, dirty, no-op), save/load round trip incl. both instruments, 0.5 `basicSynth` compat, out-of-range corrected, unknown settings and instruments preserved | nothing |
| `app_open_*` | Real app opens fixture files: valid demo (0 warnings), corrected tempo (1 warning listed), newer format refused, truncated JSON refused | X server |
| `[realtime]` | **Callback path makes zero heap allocations** (global `operator new` counter), plus a self-check that the counter works | nothing |
| `[live]` | Live notes sound one block after arrival at the exact sample for block sizes 256/480/1024; need a synth target; note off / all-notes-off / a new target release them; held live notes survive play, seek, loop wraps, clip edits and Stop; allocation-free | nothing (fake clock) |
| `[live][record]` | Recorded at the tick the player heard (tick 192 for a press at 0.1 s @ 120 BPM, any block size); output latency compensated also across a loop wrap; punch-out ends the take; nothing recorded while stopped or not recording; **bursty callbacks (two blocks back to back) keep exact spacing** in sound and recording | nothing (fake clock) |
| `[record]` | Takes: new clip from the note's bar growing bar by bar, one undo step; overdub into the clip under the note; loop recording fills the loop region, a note held over the wrap ends at the loop end; held keys closed at the end; a take's clip never grows over another clip; only synth tracks record | nothing |
| `[session][tracks]` (reorder) | Move track up/down/clamped, slots unchanged, undo | nothing |
| `[midi][file]` | SMF write → read round trip (format 1, PPQ, 3-byte delta times, meta/program events); broken and truncated files refused with reasons | nothing |
| `[midi][export]` | Export: tempo 124 → 483871 µs, 3/4 signature, track names, GM drums on channel 10 as played (muted lane left out), synth program + channel, notes cut at clip end, songs end; mute/solo respected; channels skip 10; atomic file write | temp dir |
| `app_export_midi` | Real app opens a project and exports it with `--export-midi` | X server |
| `[export]` | **Offline render = live playback, sample for sample** (song and loop range); song length exact at 44.1/48/96 kHz and at 133 BPM; tails ring out then stop; loop range exact without tail; cancel leaves no file and keeps an existing one; empty song / bad folder refused | temp dir |
| `[export][wav]` | 16/24-bit and float WAV at 44.1/48/96 kHz: header fields, frame count, exact file size; float bit-exact, 24-bit within 2 LSB; integer clipping counted and clamped, float overs kept; dithered silence ≤ 1 LSB | temp dir |
| `app_export_wav` | Real app renders a project to 16-bit / 44.1 kHz WAV with `--export-wav` | X server |
| `[api]` | **The Phase 10 scenario through the command API** (state, tempo, track, clip, notes by name/number, play with sound, WAV export job with validated result); invalid arguments change nothing (types, ranges, unknown ids, wrong track type, all-or-nothing notes); one undo step per call; save/open | temp dir |
| `[api][safety]` | Files only inside the allowed folder (absolute paths, `..`, symlinks out refused; extensions enforced); unsaved work, deletes and overwrites need confirmation | temp dir |
| `[ipc]` | Requests over a real Unix socket, answered in order with their ids; broken JSON / missing op / bad args → structured errors, connection keeps working; socket 0600 in a 0700 folder; a live socket is never taken over, a stale one is replaced; oversized lines refused | temp dir |
| `[session][undo]` (names) | Undo steps are named; gestures and AI calls name a whole step; redo labels | nothing |
| `[presets]` | Every preset in range, Init = defaults, recognised as current; loading is one undo step; **all poly × bass preset combinations with chords + bass + a busy beat stay below 0 dBFS** | nothing |
| `[mix]` (meters, master) | Track meters post-fader, muted track reads 0; master -6 dB halves the tracks, the metronome is not affected; master volume saved/loaded/undoable/limited | nothing |
| `[session][clips]` (paste) | Paste onto a track of the same kind with fresh ids; refused across kinds; named undo | nothing |
| `[live][record]` (take start) | **A key pressed as the take starts lands on its first beat, not at the loop end**; a press early in the second pass still wraps | nothing (fake clock) |
| `mcp_server` | MCP server (Node): results and app error codes pass through; timeouts; app not running / quitting mid-request; only implemented tools listed (no pause, no effects); invalid arguments never reach the app | Node.js + `mcp/node_modules` |
| `app_smoke_launch` | Real GUI app starts, window reaches the screen, app quits cleanly. **Fails on any JUCE assertion or leaked object.** | X server (uses `xvfb-run` if installed) |

App tests run with `HOME=build/<preset>/tests/app-test-home`, so they never
touch your settings, recent files or MIDI device choices.

### MIDI without a keyboard

Linux always has the ALSA port **Midi Through Port-0** (14:0), which BeatForge
enables like any input. Anything sent to it reaches BeatForge as if played:

```bash
aplaymidi -p 14:0 some.mid                         # "play" a MIDI file into BeatForge (live or recording)
aseqdump -p 14:0 & aplaymidi -p 14:0 export.mid    # check an exported file with ALSA's own parser
aseqnet &                                          # a new port appears: tests hot-plug (kill it to unplug)
```

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

## Manual checklist — Phase 11

Follow [USER_GUIDE.md](USER_GUIDE.md) to make a four-bar track (drums, Acid
bass, Soft Pad chords) without touching any file by hand, then:

1. **Mixer** (Ctrl+3): balance it; meters move per track; master fader works.
2. **Undo/Redo buttons:** hover them - they name the step; Ctrl+Z removes a whole recorded take.
3. **Ctrl+S**, then **Ctrl+O** - the browser shows the project with tempo/length; open it again.
4. **Ctrl+Shift+E** - export; play the WAV.
5. **Autosave:** change something, wait a minute (indicator: Autosaved), then kill BeatForge (`pkill -9 BeatForge` is fine for this test) and start it again - it offers to restore your change.
6. **F1** shows the shortcuts; copy a clip with Ctrl+C and paste it with Ctrl+V on another track.
7. In Claude Code: "Add a lead melody to my BeatForge song on a new track with the Bright Lead preset, then export it" - check it in the app and the WAV.

## Manual checklist — Phase 10

1. Launch BeatForge: the status bar ends with **MCP ready**.
2. In a new Claude Code session (any folder), ask: "Is BeatForge connected? Show me my project." — it lists your tracks and clips.
3. "Set the tempo to 128." — the BPM field changes.
4. "Create a synth track called Lead with Nebula Poly." — it appears in the arrangement and is selected.
5. "Create a 2-bar clip on Lead at bar 1 and add the notes C4 E4 G4 C5 one beat each." — the piano roll shows them.
6. "Start playback." — you hear it; then "stop".
7. "Export the song as a 48 kHz 24-bit WAV called mcp-test and tell me whether it worked." — Claude reports the length and peak; the file is in ~/Music/BeatForge.
8. Ctrl+Z in BeatForge undoes the last change Claude made. Ask for something invalid (e.g. "set the tempo to 999", "export to /etc") — Claude reports the error and nothing changes. Ask it to start a new project with unsaved changes — it must ask you first.

## Manual checklist — Phase 9

1. Open (or make) a short song with drums, bass and a melody. Listen to it once with Play.
2. **Ctrl+Shift+E** (or project menu → Export Audio...). Choose **Whole song**, **48 kHz**, **24-bit**, press **Export...**, keep the suggested name next to your project, Save.
3. The progress bar runs; then the summary shows the length (song + tail), the format and the peak.
4. Open the .wav in a media player or audio editor (e.g. VLC, Audacity): same song, same balance, ends with the notes ringing out — no metronome, no loop.
5. Export again as **Loop region**, **16-bit**, **44.1 kHz**; set your player to repeat — it loops without a gap or click.
6. Start an export and press **Cancel** — "Export cancelled. No file was written."
7. Optional: in Audacity check the format (Project rate / sample format) and the length match what you chose.

## Manual checklist — Phase 8

0. No MIDI keyboard? Press **K**: the keyboard button lights and the status bar shows "Keys C4-G5, velocity 100". Select a synth track and play **Q W E R T Y U I** (C major) and **2 3 5 6 7** (sharps); **Z/X** change the octave, **C/V** the velocity. Use it wherever the steps below say "play"; record with **Ctrl+R** instead of R.
1. Plug in your MIDI keyboard (before or after launching). The status bar shows **MIDI in: <your keyboard>**; the light flashes when you play. (Audio Settings lists it under Active MIDI inputs.)
2. Select a synth track (Bass or Synth) and play — you hear it. Select the Drums track — the keyboard goes quiet.
3. Turn the metronome on (**M**), select a synth track, press **R** (or the red Record button): playback starts. Play a phrase, press **Space** to stop. The take appears as a clip and opens in the piano roll; notes sit where you played them.
4. **Ctrl+Z** removes the whole take; **Ctrl+Shift+Z** brings it back.
5. Set a 2-bar loop on the ruler, press **R** and play over several passes — everything lands in one 2-bar clip (overdub). No keyboard? Click the piano roll keys while recording.
6. **Ctrl+E** → export a .mid file; open it in another MIDI app (MuseScore, LMMS, an online MIDI player) — tracks, notes, tempo and drums are there.
7. Arrangement: **Ctrl+wheel** to zoom, **Shift+wheel** / scroll bar to scroll, **Fit** to see the whole song; play while zoomed in — the view follows. Drag a track header up or down to reorder tracks (colours stay with the tracks).

## Manual checklist — Phase 7

1. Launch. The arrangement is on top: a **Drums** track with a 1-bar clip and an empty **Synth** track. Program a beat in the step sequencer below.
2. Drag the drum clip's right edge to bar 9 — the pattern now repeats for 8 bars.
3. **+ Synth** → a new track appears and is selected. Instrument tab → **Nebula Bass**. Double-click its lane at bar 1 → a 4-bar clip opens in the piano roll; draw a bassline. Double-click the track name and rename it "Bass".
4. Click the bass clip, press **Ctrl+D** — a copy appears right after it (bars 5–8).
5. On the **Synth** track double-click at bar 1, draw a melody, then drag the clip to bar 5.
6. Click the ruler at bar 1, press **Space** — 8 bars play: drums + bass, the melody joins at bar 5; it stops making sound after bar 8. Try **M**/**S**, volume and pan on the tracks while it plays.
7. Drag across the ruler's thin top strip from bar 3 to bar 5 — looping turns on; **Space** loops bars 3–4. Click the ruler elsewhere to move the playhead; **Stop** returns there.
8. Move or delete a clip, then **Ctrl+Z** / **Ctrl+Shift+Z** — each drag or delete is undone/redone in one step.
9. **Ctrl+S**, quit, reopen — tracks, clips, mix and loop come back and it sounds the same. Optional: open a 0.6 project — it appears as a drum clip + synth clip at bar 1.

## Manual checklist — Phase 6

1. Program a beat (Drums tab). **Ctrl+3** → choose **Nebula Bass**.
2. **Ctrl+2** → create a clip, scroll down to C1–C2, draw a bassline on the off-beats.
3. Press **Space** — bass and drums together.
4. While it plays, try the wave buttons, turn **Cutoff**, **Resonance**, **Env Amt**, **Decay** — the sound changes smoothly; the displays follow.
5. Overlap two notes and raise **Glide** — the bass slides between them.
6. Switch to **Nebula Poly**, draw a chord — all notes sound; turn Attack/Release.
7. Save, reopen — instrument and settings come back.

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
| 2026-10-18 | 11b (foundation, chrome, Instrument) | 177/177 ctest pass in Debug and Release. Xvfb screenshots at 1920×1080 compared side by side with `docs/ui-spec/BeatForge UI Revamp/BeatForge — Lead · Instrument.png`: header, arrangement, master panel, status bar and Instrument tab match the layout; at 1280×800 the header drops the version, TIME cell and wordmark without overlaps. Playing state (metronome to a virtual sink): beat dot, lit metronome toggle, moving readouts, meter gradient and dB PEAK readout (-6.3) all render. Zero JUCE assertions. No literal colours outside Theme.h in the restyled files. Not yet tested on your display. |
| 2026-10-18 | 11 | 177/177 ctest pass in Debug and Release (169 unit cases). **4-bar track made entirely in the UI** (Xvfb, sandboxed HOME): tempo 124, 4-bar drum pattern, Acid bass recorded with the computer keyboard into a 4-bar loop (16 notes on the beats) and quantized, Soft Pad chords recorded (Am F C G, 12 notes), mixed in the Mixer (Pad -7 dB, master -2.2 dB), saved, reopened through the project browser, exported from the dialog: **8.77 s (7.7 s + 1.0 s tail), 48 kHz 24-bit, peak -6.8 dBFS**, ffprobe OK, steady level per bar. **Then changed through MCP** (real server, SDK client): tempo 128, new Lead track with Bright Lead preset and a 13-note melody, snare backbeat added (other lanes kept), Lead -3 dB, played, saved, exported "Four Bars v2" (7.5 s music); a solo export confirmed all 13 melody pitches. Crash recovery: change → autosaved → kill -9 → restart offered Restore → change back and unsaved → Ctrl+S wrote it, recovery removed. Idle CPU (Release) 4.6% → 2.1% of a core (UI thread 0.8%). Found & fixed: **notes played as a looped take started were moved to the loop end** (regression test); undo icons unclear; tooltips cut to one line; the clip-length tooltip was stale; Escape did not close the project browser from its search box; the shortcuts list was clipped; the export dialog showed a stale length after a tempo change; export errors lacked next steps. Seen only under Xvfb: the area a closed dialog covered is repainted on the next UI event. Added after review: menu button separate from the project name (double-click renames in place: checked), Help (?) button beside the computer-keyboard button → User Guide (guide built into the app; checked rendering of headings, numbered and bullet lists with wrapped lines, bold/code, scrolling to the end). User hands-on test: approved 2026-10-18. |
| 2026-10-18 | 10 | 168/168 ctest pass in Debug and Release (160 unit cases, 7 app tests, MCP server tests 9/9). Real app (Xvfb, sandboxed HOME and socket) driven through the real MCP server with the SDK client (`mcp/scripts/e2e.mjs`): read state → tempo 128 → track "MCP Lead" (appeared and selected) → 2-bar clip → 7 notes (piano roll showed them) → play: transport 3.1 after 4 s, captured audio **C4 E4 G4 C5 G4 E4 C4 C4** per beat (one G4 read as G3 by the pitch detector; the app's clip data confirms G4) → stop → export job polled to done: **4.006 s (3.75 s = 2 bars at 128 + tail), 48 kHz 24-bit, validated**, ffprobe pcm_s24le 48000 Hz, same per-beat pitches as playback. Errors: 999 BPM (MCP validation), /etc path (path_not_allowed), new project with unsaved changes (confirmation_required). Socket 0600 in a 0700 folder; requests worked while the quit prompt was open; quitting with a client connected: clean exit, socket removed, next call "not_running", connection status "connected: false". Registered with Claude Code (`claude mcp get beatforge`: Connected). User hands-on test from Claude Code: approved 2026-10-18. |
| 2026-10-18 | 9 | 157/157 ctest pass in Debug and Release (150 unit cases). Offline render vs live engine: **0 differing samples** (song and loop range). Real app (Xvfb, sandboxed HOME): the 8-bar Phase 7 song exported from the dialog (48 kHz 24-bit) in under a second: **16.064 s** (16.0 s + tail), ffprobe: pcm_s24le 48000 Hz stereo; per-bar RMS within 0.1-0.2 dB of the Phase 7 live capture, same **-3.7 dBFS** peak. CLI: 16-bit 44.1 kHz (pcm_s16le, 16.070 s), 32-bit float 96 kHz (pcm_f32le, 16.053 s), loop bars 3-6 = **6.000 s** exactly. 128 s song: progress bar + percentage, **Cancel** removed the .tmp and wrote nothing; exporting while playing: **0 dropouts**; closing the dialog mid-render: no hang, temp file gone, existing file of the same name untouched; clean quit. Found & fixed: the percentage did not update. User hands-on test: approved 2026-10-18. |
| 2026-10-18 | 8 | 150/150 ctest pass in Debug and Release (144 unit cases, 529,951 assertions). Real app (Xvfb, sandboxed HOME, virtual sink, ALSA Midi Through as the keyboard): a MIDI file played into a selected synth track sounded **C4 E4 G4 C5 G4 E4 C4 D4 at 0.5 s** (±10 ms onset resolution). Recorded with **R**: 8 notes with their velocities (100…127); first build showed ±10 ms spacing jitter (954–979 ticks for 960) caused by bursty ALSA→PipeWire callbacks → engine now follows the audio clock → **960 ticks ×6 and 962, durations 480/482 for 480 sent** (≤ 2 ticks ≈ 1 ms). Take = one undo step; loop recording into a new track made one 2-bar loop clip; clicks on the piano keys recorded 3 notes. Ctrl+E dialog export (3 tracks, 19 notes) read back by ALSA's parser (`aplaymidi`→`aseqdump`): 19 note on/off pairs, right pitches, velocities, channels, programs. Hot-plug: a new ALSA port (aseqnet) appeared in the status bar within 2 s, enabled and saved; unticking in Audio Settings disabled and saved it. Zoom buttons, Ctrl+wheel, Shift+wheel, Fit, follow while playing and back to the start on Stop; track drag + Move down + undo. Clean quit after recording. Computer keyboard (K): Q W E R T Y U I sounded **C4 D4 E4 F4 G4 A4 B4 C5**, 2 → C#4; a 1.2 s hold gave one note (no auto-repeat retrigger); recorded with Ctrl+R: lengths **0.265 / 1.215 / 0.515 s for 0.25 / 1.2 / 0.5 s holds**, X → G5, V → velocity 120; with the piano roll focused Q and R played notes (no quantize, no recording); K off → R records again; setting remembered. Found & fixed: device state with the enabled MIDI inputs was never saved (JUCE only queues change messages for listeners already registered) → MIDI input lost on the next launch; app ctests were writing to your real settings (now own HOME; your file untouched); track colours swapped when reordering; piano roll did not show a fresh take; clip labels vanished when scrolled; length box said "3.00 bars"; after Stop the view stayed away from the playhead. User hands-on test (computer keyboard; exported .mid opened in another app): approved 2026-10-18. |
| 2026-10-18 | 7 | 126/126 ctest pass in Debug and Release (121 unit cases, 529,726 assertions). Real UI (Xvfb, sandboxed HOME, virtual sink): programmed a beat, stretched the drum clip to 8 bars, added a synth track → Nebula Bass, double-clicked a 4-bar clip and drew 16 notes, **Ctrl+D** duplicated it to bar 5, made a melody clip and dragged it from bar 1 to bar 5. Full 8-bar playback (loop off): level steady over bars 1–8, silence after (−57 dB), peak −3.7 dBFS. Solo bass: **A#2×4 C3×4 D#3×4 G2×4** twice at 0.5000 s; solo melody: first note **8.0 s after start** (bar 5), **C4 A3 G3 F3 E3 G3 A3 C4** at 1.000 s. Seek to bar 3 + loop 3–5: bass **D#3 G2 D#3 G2…**, 0.5000 s across wraps, Stop back at 3.1.000. Move + Delete → Ctrl+Z ×2 → Ctrl+Shift+Z each one step. Rename track, add/delete track, saved (v2 JSON checked) and reopened with clips, instrument and loop intact; quit prompt; clean exit. Each capture showed one 20–25 ms short gap at a random time (5.9/6.2/11.7/15.7 s) while the app counted 0 dropouts: lost in the ALSA→PipeWire capture path, not the engine (scheduling errors would repeat at the same musical position). Found & fixed: a loop drag included the bar under the release point (3→5 gave 3–6); step-sequencer subtitle said "for 1 bars" and was clipped. User hands-on test: approved 2026-10-18. |
| 2026-10-18 | 6 | 118/118 ctest pass in Debug and Release (15 new). Real UI (Xvfb, sandboxed HOME, virtual sink): Instrument tab screenshots match the Nebula reference with live displays; chose Nebula Bass (Glide shown, A/S/R hidden); programmed drums + drew an 8-note off-beat bassline; bass alone analysed with YIN: **C2 C2 D#2 C2 G1 G1 A#1 C2** at **0.5 s** spacing; switching to square + cutoff 13.4 kHz while playing → display followed, capture **1.4× brighter**; saved and reopened with instrument/settings restored; your own `save test` / `save2` projects load read-only in 0.6 without warnings (unchanged on disk). Found & fixed: envelope release stretched by per-block parameter updates (and buffer-size dependent); filter pacing depended on the block size; **mix clipped (+3.2 dBFS) with chords + drums** → gain staging (kit −6 dB, Poly −10 dB, Bass −6 dB default), typical mix now **−4.5 dBFS**; settings could be lost when no clip existed. User hands-on test: approved 2026-10-18. |
| 2026-10-18 | 5 | 103/103 ctest pass in Debug and Release (24 new). Real UI (Xvfb, sandboxed HOME, virtual sink): created a 4-bar clip, set 1/8, clicked an 8-note melody (8 auditions heard), dragged C5 → one eighth later + D5, stretched F4 to 3 eighths, dragged a velocity bar; playback analysed note by note: onsets **0 / 0.5 / 1.0 / 1.75 / 2.0 / 2.5 / 3.0 / 3.5 s**, pitches **C4 E4 G4 D5 B4 G4 A4 F4**, quieter C4 (−5.8 dB), F4 sounding through 4.05 s; off-grid note quantized with Q; Delete, Ctrl+A, Up/Shift+Up work; saved and reopened from the command line with all notes. Found & fixed: notes fired one sample early at some block sizes (event window now sample-exact), loop-wrap release missed when a wrap ended a block, piano-roll **Ctrl+Q quantized instead of quitting** (and Ctrl+M/L toggled), keyboard drawing. User hands-on test: approved 2026-10-18. |
| 2026-10-18 | 4 | 79/79 ctest pass in Debug and Release (21 new: file format, session, 4 app-level open tests). Isolated real-UI run (Xvfb, `HOME` sandboxed, virtual sink): built a 124 BPM beat by mouse, renamed Kick → "808 Kick", muted Snare, **Ctrl+S** via the JUCE browser → project named "Night Drive", **Saved**, readable JSON on disk; quit (no prompt); relaunched → **Open Recent** → identical state, playback **0.4839 s** intervals before and after (124 BPM); edit → quit prompt → **Cancel** keeps app open; **Ctrl+S** wrote directly; Ctrl+Shift+S, Escape, Space all work with real key input; clean exit, 0 assertions. Found & fixed: alert icon clipped; tests wrote recent files into real settings; settings were in `~/BeatForge` (moved to `~/.config/BeatForge`, migrated); zenity 4.0.1 ignored the suggested file name (switched to JUCE's themed browser); shortcuts made window-level. User hands-on test: approved 2026-10-18. |
| 2026-10-18 | Nebula 1 | 58/58 ctest pass. Xvfb UI session with the restyled app: house beat entered by mouse, loop/solo/mute toggled, captured mix identical to pre-restyle (**0.5000 s** grid, peaks **-1.5 / -5.4 dBFS**); screenshots of main view, dropdown menu, tooltip and Audio Settings dialog all themed; clean shutdown, zero JUCE assertions. No hard-coded colours outside Theme.h. |
| 2026-10-18 | 3 | 58/58 ctest pass in Debug and Release. Real UI driven in Xvfb (audio to a virtual PipeWire sink): clicking steps while stopped auditioned 6 hits; drag-painted 16 hats; played the user-checklist beat — solo captures: kick **0.5000 s**, clap **1.0000 s**, hats **0.1250 s** @ 120 BPM; typed 100 BPM mid-playback → **0.6000 s**; muting the kick left hats at **0.1500 s**. Found & fixed: default kit clipped (+3.2 dBFS on kick+hat+clap; red clip lights on kick+snare) → kit rebalanced, busy house beat now peaks **-1.5 dBFS, 0 clipped**; ruler lost the playhead after bar 8 → pages now; dangling-reference crash in a test. `--smoke-play` on real device OK. User hands-on test: approved 2026-10-18. |
| 2026-10-18 | 2 | 40/40 ctest pass in Debug and Release (178k assertions incl. a 1-hour no-drift run). `--smoke-play` on real device: 2.01 s audio / 2.01 s wall, 5 beats @ 120, 0 dropouts. Virtual-sink capture: click intervals **0.5000 s** @ 120 BPM, downbeat -6.3 dBFS vs -10.8 dBFS. Real UI driven in Xvfb with xdotool: typed 100 BPM → **0.6000 s**; dragged tempo during playback → clean transition to 143 BPM (**0.4196 s**), no doubled/missing click; 3/4 + 1-bar loop → constant 0.4196 s across loop wraps, accent every 3rd click; Space/M/L keys, Ctrl+Q clean exit, zero JUCE assertions. User hands-on test on speakers: approved 2026-10-18. |
| 2026-10-17 | 1 | 19/19 ctest pass in Debug and Release. On real hardware (`:1`, ALSA → PipeWire → AirBass Earbuds BT sink, 44.1 kHz/512): `--smoke-play` rendered 2.02 s of audio in 2.02 s wall (ratio ~1.00), 0 dropouts, stopped cleanly. Sink-monitor capture: 440 Hz at **-18.0 dBFS** for ~2.1 s, silence after Stop. No-device path (broken ALSA config): red status, Play disabled, no crash. User hands-on test on speakers: all checks pass — approved 2026-10-18. |
| 2026-10-17 | 0 | 3/3 automated tests pass (Debug, Xvfb). Launched on real X11 display `:1`, window 1280×800, clean shutdown. User hands-on test: all 5 checks pass — approved. |
