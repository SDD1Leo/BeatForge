# BeatForge MCP server

Lets an AI assistant (e.g. Claude Code) inspect and edit the **running**
BeatForge app. It is a small TypeScript program speaking MCP over stdio; it
talks to BeatForge through a local Unix socket and never opens a network port
or runs shell commands. Every operation goes through BeatForge's command API
(`core/include/beatforge/api/CommandApi.h`), the same validated, undoable path
as the UI.

## Install and register

```bash
cd ~/e_drive/BeatForge/mcp
npm ci && npm run build          # needs Node.js 20+
claude mcp add --scope user beatforge -- node ~/e_drive/BeatForge/mcp/dist/index.js
```

Start BeatForge (the status bar shows **MCP ready**), then start a new Claude
Code session (or `/mcp` to reconnect). Ask e.g. *"What's in my BeatForge
project?"*. Remove with `claude mcp remove beatforge -s user`.

## Tools

| Area | Tools |
|---|---|
| Connection | `get_connection_status` |
| Project | `get_project_state`, `create_project`*, `save_project`, `open_project`*, `undo`, `redo` |
| Transport | `get_transport_state`, `get_tempo`, `set_tempo`, `set_time_signature`, `set_loop`, `set_metronome`, `play`, `stop`, `seek` |
| Tracks | `list_tracks`, `create_track`, `rename_track`, `delete_track`*, `set_track_volume`, `set_track_pan`, `set_track_mute`, `set_track_solo` |
| Clips & notes | `create_clip`, `delete_clip`*, `set_drum_pattern`, `get_clip_notes`, `add_midi_notes`, `remove_midi_notes`*, `quantize_clip` |
| Instruments | `list_instruments`, `set_instrument`, `set_instrument_param` |
| Export | `export_wav` (→ job id), `get_export_status`, `cancel_export`, `export_midi` |

\* destructive: fails with `confirmation_required` unless called with
`confirm: true` (the assistant must ask you first). Replacing an existing file
needs `overwrite: true`. Not offered because BeatForge does not have them yet:
pause, effects.

Units: bars and beats are 1-based for positions in the song; note positions
inside a clip are beats from the clip start (0 = first beat). Pitches are MIDI
numbers or names (`C4` = 60).

## Safety

- **Local only:** the socket is in a private folder (mode 0700) and is mode
  0600; BeatForge also checks that each connection belongs to your user.
- **Files:** only inside `~/Music/BeatForge` (relative names go there;
  `..`, other folders and symlinks leading out are refused).
- **Validation twice:** arguments are checked here (zod) and again in the app;
  a bad request changes nothing (e.g. `add_midi_notes` adds all notes or none).
- **Errors** come back as tool errors with a code: `invalid_argument`,
  `not_found`, `confirmation_required`, `path_not_allowed`, `no_audio_device`,
  `not_running`, `disconnected`, `timeout`, …
- **Timeouts:** 15 s per request here, 10 s in the app; a request the app did
  not start in time is never applied later.

## Socket path and limits

`$BEATFORGE_SOCKET`, else `$XDG_RUNTIME_DIR/beatforge/daw.sock`, else
`/tmp/beatforge-<uid>/daw.sock`. Linux only (Unix domain sockets). One
BeatForge instance serves at a time (a second one shows "MCP off"). Requests
are handled one at a time on the app's main thread.

## Tests

`npm test` (fake app socket: validation, timeouts, disconnects, tool list).
`node scripts/e2e.mjs` runs the Phase 10 scenario against a running BeatForge
— it edits the open project and writes `~/Music/BeatForge/mcp-e2e.wav`.
