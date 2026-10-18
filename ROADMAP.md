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
| 8 | MIDI device input, recording, MIDI file export | ⏳ |
| 9 | Offline render & WAV export | ⏳ |
| 10 | MCP server (TypeScript, local IPC) | ⏳ |
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
- No arrangement zoom or horizontal scroll yet: the view shows at least 16 bars
  and grows with the song. Tracks cannot be reordered yet. At most 16 tracks.
- Drum clips hold one 16-step pattern (a 4/4 bar of 16ths) that repeats every bar
  of the clip; in other time signatures it still repeats every 16 sixteenths.
  No per-clip pattern variations, step velocity/accent or swing yet.
- A clip overlapping a later clip on the same track is cut where the later one starts.
- Opening a project does not stop playback first.
- Grid choice in the piano roll is not saved. Selection is not part of undo.
- Instruments: one oscillator per voice, low-pass only, no LFO/modulation
  matrix/effects/presets yet (the Nebula reference shows these as future).
- In Debug builds, if the ALSA MIDI sequencer cannot be opened JUCE raises a
  debug assertion (`juce_Midi_linux.cpp`). Seen only with ALSA deliberately broken.

## Next up: Phase 8

MIDI device input (play instruments from a keyboard), recording into clips,
and MIDI file export.
