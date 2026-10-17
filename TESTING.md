# Testing BeatForge

```bash
ctest --preset debug
```

## Automated tests

| Test | What it proves | Needs |
|---|---|---|
| `[buildinfo]` unit tests | Version metadata wired correctly from CMake; JUCE pinned to 9.0.4 | nothing |
| `[tone]` | 440 Hz at 44.1/48/96 kHz, -18 dBFS, click-free start/stop, identical output for any block size, same signal on L/R | nothing |
| `[engine]` | Silence when stopped (clears garbage), tone on first stereo pair only, position advances only while playing and rewinds after the fade, 500 play/stop cycles, device restart at a new rate | nothing |
| `[meter]` | Peak accumulation/reset, dB conversion, meter reads -18 dBFS for the tone | nothing |
| `[realtime]` | **Callback path makes zero heap allocations** (global `operator new` counter), plus a self-check that the counter works | nothing |
| `app_smoke_launch` | Real GUI app starts, window reaches the screen, app quits cleanly. **Fails on any JUCE assertion or leaked object.** | X server (uses `xvfb-run` if installed) |

Manual / on-device commands (not in `ctest`, they make sound):

```bash
# Press Play, verify the device clock keeps real time for 2 s, Stop, verify stopped.
./build/debug/app/BeatForge_artefacts/Debug/BeatForge --smoke-play

# Additionally capture what reaches PipeWire's output (default sink monitor):
pw-record -P '{ stream.capture.sink=true }' --rate 48000 --channels 2 --format f32 /tmp/cap.wav &
./build/debug/app/BeatForge_artefacts/Debug/BeatForge --smoke-play; kill %1
```

Simulate "no audio device": `ALSA_CONFIG_PATH=/path/to/empty-file BeatForge`.

Proof levels we keep separate (from Phase 1 on):

1. **Code behaviour** — unit tests on pure logic (conversions, scheduling).
2. **Generated audio** — render to buffers/files and analyse samples.
3. **Real device** — only a human (or a run on real hardware) proves sound
   actually comes out of the speakers. Automated tests never claim this.

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
| 2026-10-17 | 1 | 19/19 ctest pass in Debug and Release. On real hardware (`:1`, ALSA → PipeWire → AirBass Earbuds BT sink, 44.1 kHz/512): `--smoke-play` rendered 2.02 s of audio in 2.02 s wall (ratio ~1.00), 0 dropouts, stopped cleanly. Sink-monitor capture: 440 Hz at **-18.0 dBFS** for ~2.1 s, silence after Stop. No-device path (broken ALSA config): red status, Play disabled, no crash. **Not yet verified by a human listener.** |
| 2026-10-17 | 0 | 3/3 automated tests pass (Debug, Xvfb). Launched on real X11 display `:1`, window 1280×800, clean shutdown. User hands-on test: all 5 checks pass — approved. |
