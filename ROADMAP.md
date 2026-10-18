# BeatForge roadmap

Each phase ends with a build, automated tests, and a hands-on test by the
user. The next phase starts only after approval.

Legend: ✅ approved · 🧪 built, awaiting user test · ⏳ planned

| # | Phase | Status |
|---|---|---|
| 0 | Environment & project foundation | ✅ |
| 1 | Audio output & transport (device selection, test tone, play/stop, level meter) | ✅ |
| 2 | Tempo engine & metronome (BPM, bars/beats, loop foundation) | ✅ |
| 3 | First drum machine (16-step sequencer, 5 drum sounds) | ✅ |
| 4 | Project save/load (versioned format) | ✅ |
| 5 | MIDI engine & piano roll | ✅ |
| 6 | Built-in synth & mono bass | ✅ |
| 7 | Arrangement view (tracks, clips, volume/pan, loop, undo) | ✅ |
| 8 | MIDI device input, recording, MIDI file export (+ arrangement zoom/scroll/reorder) | ✅ |
| 9 | Offline render & WAV export | ✅ |
| 10 | MCP server (TypeScript, local IPC) | 🧪 |
| 11 | Polish & creative workflow | ⏳ |
| 12 | Advanced features (one at a time) | ⏳ |

**First major goal:** a complete four-bar electronic composition, exported.

## Known limitations

- Changing device / sample rate / buffer size stops playback (device restart).
- JACK backend disabled; ALSA-via-PipeWire only. Default buffer is 512 samples
  (~46 ms reported output latency) — fine for now, tune in Audio Settings.
- One time signature for the whole project (time-signature changes later).
- Clip positions snap to beats, loop points to bars; no free/fine snapping yet.
  No pause: Stop returns to where playback started (set it by clicking the ruler).
- Drum clips hold one 16-step pattern (a 4/4 bar of 16ths) that repeats every bar
  of the clip; in other time signatures it still repeats every 16 sixteenths.
  No per-clip pattern variations, step velocity/accent or swing yet.
- A clip overlapping a later clip on the same track is cut where the later one starts.
- Opening a project does not stop playback first.
- Grid choice in the piano roll is not saved. Selection and the arrangement view
  (zoom/scroll) are not saved.
- Instruments: one oscillator per voice, low-pass only, no LFO/modulation
  matrix/effects/presets yet (the Nebula reference shows these as future).
- MIDI input: all channels are merged; sustain pedal (CC64), pitch bend,
  modulation and aftertouch are ignored; live input plays synth tracks only (not
  drums). No count-in, input quantize or MIDI import yet. A take is recorded
  only into the selected synth track.
- Audio export: WAV only (no MP3/FLAC yet), stereo, 44.1/48/96 kHz; no
  normalising or limiting; the tail stops at -100 dBFS or after 4 s. The
  metronome is never exported.
- MCP: Linux only (Unix socket); one BeatForge instance serves at a time;
  files only in ~/Music/BeatForge; no pause and no effects (not in the app
  yet); requests run one at a time on the app's main thread (10 s timeout).
- MIDI export writes the whole song from bar 1 (what you hear: muted tracks and
  lanes are left out); drum notes are fixed 1/32 long at velocity 100.
- In Debug builds, if the ALSA MIDI sequencer cannot be opened JUCE raises a
  debug assertion (`juce_Midi_linux.cpp`). Seen only with ALSA deliberately broken.

## Next up: Phase 11

Polish and creative workflow: interface consistency, shortcuts, project
browser, instrument controls, mixer, error messages, save indicators,
undo/redo, tooltips, performance, documentation — tested by making a four-bar
track entirely in BeatForge and then changing it through MCP.
