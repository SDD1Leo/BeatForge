# BeatForge user guide: your first track

This walks you through a four-bar electronic track: drums, bass, chords and a
lead, mixed, saved and exported. It takes about ten minutes. Every step can be
undone with **Ctrl+Z**; **F1** shows all keyboard shortcuts.

## The screen

- **Top:** project menu (name, saved state), undo/redo, transport (Play, Stop,
  Record, Loop), tempo / time signature / position, metronome, computer
  keyboard, audio settings.
- **Middle:** the arrangement: one row per track, clips on a timeline, the ruler
  (loop region on its thin top strip, playhead below).
- **Bottom:** the editor for what you selected: **Step Sequencer** (drum clips) or
  **Piano Roll** (synth clips), **Instrument**, **Mixer** (Ctrl+1/2/3).
- **Right:** the master level meter. **Status bar:** audio device, CPU, MIDI
  inputs, computer keyboard, MCP.

## 1. Tempo and loop

1. Double-click the BPM field, type **124**, press Enter.
2. Press **L**: the loop (bars 1-4 on the ruler's top strip) turns on. Drag on
   the strip to change it.

## 2. Drums

1. A new project has a **Drums** track with a 1-bar clip. Drag the clip's right
   edge to bar 5 - the pattern repeats for four bars.
2. In the Step Sequencer click steps: Kick 1, 5, 9, 13; Clap 5, 13; Closed Hat
   3, 7, 11, 15. Press **Space** to listen.

## 3. Bass

1. Click **+ Synth**. On the **Instrument** tab choose **Nebula Bass** and the
   preset **Acid** (or **Sub** for a deep bass).
2. Double-click the track name and call it "Bass".
3. Play it: with a MIDI keyboard just play; without one press **K** to use the
   computer keyboard (Q W E R T Y U I = white keys, 2 3 5 6 7 = black keys,
   Z / X = octave). Press **Z** twice for a bass octave.
4. Press **Ctrl+R** (or the red Record button): the loop plays and records. Play
   one note per beat: A for a bar, then F, C, G. Press **Space** to stop.
5. On the **Piano Roll** press **Quantize** to tighten the timing. Not happy?
   **Ctrl+Z** removes the whole take; record again.

## 4. Chords

1. Click the **Synth** track, choose the preset **Soft Pad**, rename it "Pad".
2. Press **X** (one octave up), **Ctrl+R**, and hold three keys per bar: Y I P
   (A minor), R Y I (F), Q E T (C), T U O (G). Stop, then **Quantize**.
   (You can also draw notes with the mouse in the Piano Roll.)

## 5. Mix

Open the **Mixer** (Ctrl+3). While the loop plays, pull the Pad down to about
-7 dB and keep the master meter below 0 dB (the top stays violet/magenta, not
red). Mute and solo are on each strip; pan with the small knob.

## 6. Save, reopen, export

1. **Ctrl+S**: name it "Four Bars". The indicator next to the name shows
   **Saved**; while you have unsaved changes BeatForge keeps a safety copy every
   minute and offers it back after a crash.
2. **Ctrl+O** opens the project browser: your projects with tempo, length and
   date. Double-click to open.
3. **Ctrl+Shift+E**: Whole song, 48 kHz, 24-bit, **Export...** - you get a WAV
   next to your project and a summary (length, peak level).

## 7. Arranging more

- Copy a clip: select it, **Ctrl+C**, select a track, click the ruler where it
  should go, **Ctrl+V**. **Ctrl+D** duplicates right after the clip.
- Zoom with **Ctrl+wheel** or the -/+ buttons; **Fit** shows the whole song.
- Drag a track header up or down to reorder tracks.

## 8. Let an AI assistant help (optional)

With the MCP server set up (see [mcp/README.md](mcp/README.md)), ask Claude Code
things like "add a lead melody on a new track with the Bright Lead preset" or
"add a snare on 2 and 4". Changes appear in BeatForge at once, are named
"AI: ..." in the undo history, and files are only written in ~/Music/BeatForge.
