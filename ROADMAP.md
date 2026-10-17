# BeatForge roadmap

Each phase ends with a build, automated tests, and a hands-on test by the
user. The next phase starts only after approval.

Legend: ✅ approved · 🧪 built, awaiting user test · ⏳ planned

| # | Phase | Status |
|---|---|---|
| 0 | Environment & project foundation | ✅ |
| 1 | Audio output & transport (device selection, test tone, play/stop, level meter) | ✅ |
| 2 | Tempo engine & metronome (BPM, bars/beats, loop foundation) | ✅ |
| 3 | First drum machine (16-step sequencer, 5 drum sounds) | ⏳ |
| 4 | Project save/load (versioned format) | ⏳ |
| 5 | MIDI engine & piano roll | ⏳ |
| 6 | Built-in synth & mono bass | ⏳ |
| 7 | Arrangement view (tracks, clips, volume/pan, loop, undo) | ⏳ |
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
- Loop always starts at bar 1 (1/2/4/8 bars); dragging loop points arrives with
  the arrangement view (Phase 7). No seek/pause yet — Stop returns to bar 1.
- One time signature for the whole project (time-signature changes later).
- The ruler is display-only.
- In Debug builds, if the ALSA MIDI sequencer cannot be opened JUCE raises a
  debug assertion (`juce_Midi_linux.cpp`). Seen only with ALSA deliberately broken.

## Next up: Phase 3

First drum machine: a 16-step sequencer with kick, snare, closed/open hi-hat
and clap; pattern looping in sync with the tempo; basic track controls.
