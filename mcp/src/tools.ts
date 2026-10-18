// The BeatForge tools. Each tool is one operation of the app's command API
// (core/include/beatforge/api/CommandApi.h); arguments are validated here
// first (zod) and again by the app. Only features that exist in the app and
// are tested are exposed - e.g. no effects, no pause (Stop returns to the
// play start).
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { BeatForgeClient, DawError } from "./ipc.js";

const track = z.union([z.number().int().positive(), z.string().min(1).max(64)])
  .describe("Track id (from list_tracks / get_project_state) or exact track name");
const clip = z.number().int().positive().describe("Clip id (from get_project_state or create_clip)");
const confirm = z.boolean().optional()
  .describe("Set true ONLY after the user explicitly agreed to this destructive step");
const overwrite = z.boolean().optional()
  .describe("Set true ONLY after the user agreed to replace the existing file");
const file = (ext: string) => z.string().min(1).max(1024)
  .describe(`File name, e.g. "My Song" or "exports/My Song${ext}". Relative names go into the BeatForge folder `
    + "(~/Music/BeatForge); other locations are refused.");
const step16 = z.string().regex(/^[xX.\-]{16}$/, "16 steps of 'x' (hit) or '.' (rest)");

type ToolDef = {
  title: string;
  description: string;
  input?: z.ZodRawShape;
};

const tools: Record<string, ToolDef> = {
  // --- project ------------------------------------------------------------
  get_project_state: {
    title: "Get project state",
    description: "The open project: name, file, unsaved changes, tempo, time signature, loop, and every track "
      + "with its clips (drum patterns as step strings, note counts for synth clips). Bars are 1-based.",
  },
  create_project: {
    title: "New project",
    description: "Replace the open project with a new default one (a Drums track with a 1-bar clip and an empty Synth track). "
      + "Fails with confirmation_required if there are unsaved changes.",
    input: { confirm },
  },
  save_project: {
    title: "Save project",
    description: "Save the project. Without 'file' it saves to its current file; a new project needs a file name.",
    input: { file: file(".beatforge").optional(), overwrite },
  },
  open_project: {
    title: "Open project",
    description: "Open a .beatforge project from the BeatForge folder. Fails with confirmation_required if there are unsaved changes.",
    input: { file: file(".beatforge"), confirm },
  },
  undo: { title: "Undo", description: "Undo the last change (each tool call that edits is one undo step)." },
  redo: { title: "Redo", description: "Redo the last undone change." },

  // --- transport ------------------------------------------------------------
  get_transport_state: {
    title: "Get transport state",
    description: "Playing or stopped, position (bar.beat.tick and seconds), where playback starts, tempo, loop, metronome, audio device.",
  },
  get_tempo: { title: "Get tempo", description: "Current tempo in BPM." },
  set_tempo: {
    title: "Set tempo",
    description: "Set the tempo (40-240 BPM). Takes effect immediately, also while playing.",
    input: { bpm: z.number().min(40).max(240) },
  },
  set_time_signature: {
    title: "Set time signature",
    description: "Set the time signature, e.g. 4/4, 3/4, 6/8.",
    input: { numerator: z.number().int().min(1).max(16), denominator: z.union([z.literal(2), z.literal(4), z.literal(8), z.literal(16)]) },
  },
  set_loop: {
    title: "Set loop",
    description: "Turn looping on/off and/or set the loop region (whole bars).",
    input: {
      enabled: z.boolean().optional(),
      start_bar: z.number().int().min(1).max(4096).optional().describe("First bar of the loop (1-based)"),
      bars: z.number().int().min(1).max(256).optional().describe("Loop length in bars"),
    },
  },
  set_metronome: { title: "Metronome on/off", description: "Turn the metronome on or off.", input: { enabled: z.boolean() } },
  play: { title: "Play", description: "Start playback from the play start (see seek). Needs an audio device." },
  stop: { title: "Stop", description: "Stop playback (and recording); the playhead returns to where playback started." },
  seek: {
    title: "Move the playhead",
    description: "Set where playback starts.",
    input: { bar: z.number().min(1).max(4096).describe("1-based bar"), beat: z.number().min(1).max(16.999).optional().describe("1-based beat") },
  },

  // --- tracks ------------------------------------------------------------------
  list_tracks: { title: "List tracks", description: "All tracks with id, name, type, instrument, mix settings and clips." },
  create_track: {
    title: "Create track",
    description: "Add a drum track or a synth track (instrument nebula_poly = polyphonic synth, nebula_bass = mono bass). Max 16 tracks.",
    input: {
      type: z.enum(["drums", "synth"]),
      name: z.string().min(1).max(64).optional(),
      instrument: z.enum(["nebula_poly", "nebula_bass"]).optional().describe("Synth tracks only; default nebula_poly"),
    },
  },
  rename_track: { title: "Rename track", description: "Rename a track.", input: { track, name: z.string().min(1).max(64) } },
  delete_track: {
    title: "Delete track",
    description: "Delete a track and its clips (undo can restore it). Needs confirm: true.",
    input: { track, confirm },
  },
  set_track_volume: { title: "Set track volume", description: "Track volume in dB (-60 to +6; 0 = unity).", input: { track, db: z.number().min(-60).max(6) } },
  set_track_pan: { title: "Set track pan", description: "Track pan from -1 (left) to 1 (right); 0 = centre.", input: { track, pan: z.number().min(-1).max(1) } },
  set_track_mute: { title: "Mute track", description: "Mute or unmute a track.", input: { track, muted: z.boolean() } },
  set_track_solo: { title: "Solo track", description: "Solo or unsolo a track.", input: { track, soloed: z.boolean() } },

  // --- clips and notes -------------------------------------------------------------
  create_clip: {
    title: "Create clip",
    description: "Create a clip on a track at a bar. On synth tracks it holds MIDI notes (add_midi_notes); on drum tracks "
      + "a 16-step pattern repeating every bar (set_drum_pattern). Returns the clip id.",
    input: {
      track,
      bar: z.number().int().min(1).max(4096).describe("Start bar (1-based)"),
      bars: z.number().min(0.0625).max(64).describe("Length in bars"),
    },
  },
  delete_clip: { title: "Delete clip", description: "Delete a clip (undo can restore it). Needs confirm: true.", input: { clip, confirm } },
  set_drum_pattern: {
    title: "Set drum pattern",
    description: "Set lanes of a drum clip's 16-step pattern (1/16 notes, repeats every bar of the clip). "
      + "Example: {\"kick\": \"x...x...x...x...\", \"clap\": \"....x.......x...\"}. Lanes not given are kept.",
    input: {
      clip,
      pattern: z.object({
        kick: step16.optional(), snare: step16.optional(), closed_hat: step16.optional(), open_hat: step16.optional(), clap: step16.optional(),
      }).strict(),
    },
  },
  get_clip_notes: { title: "Get clip notes", description: "A clip's notes (id, pitch, name, start and length in beats from the clip start, velocity) or drum pattern.", input: { clip } },
  add_midi_notes: {
    title: "Add MIDI notes",
    description: "Add notes to a synth clip in one undo step. Positions are in beats of the time signature, counted from the clip "
      + "start (0 = first beat). Pitch is a MIDI number or a name like \"C4\" (= 60, middle C), \"F#3\", \"Bb2\". "
      + "All notes are checked first: if one is invalid, none are added.",
    input: {
      clip,
      notes: z.array(z.object({
        pitch: z.union([z.number().int().min(0).max(127), z.string().regex(/^[A-Ga-g][#b]?-?\d$/, "a name like C4, F#3, Bb2")]),
        start_beat: z.number().min(0),
        length_beats: z.number().min(1 / 64),
        velocity: z.number().int().min(1).max(127).optional().describe("Default 100"),
      }).strict()).min(1).max(1024),
    },
  },
  remove_midi_notes: {
    title: "Remove MIDI notes",
    description: "Remove notes (by id from get_clip_notes, or all: true) from a synth clip. Needs confirm: true.",
    input: { clip, note_ids: z.array(z.number().int().positive()).min(1).optional(), all: z.boolean().optional(), confirm },
  },
  quantize_clip: {
    title: "Quantize clip",
    description: "Move every note start in a synth clip to the nearest grid line.",
    input: { clip, grid: z.enum(["1/4", "1/8", "1/16", "1/32"]) },
  },

  // --- instruments ---------------------------------------------------------------------
  list_instruments: { title: "List instruments", description: "The built-in instruments and their parameters (ranges, units, defaults)." },
  set_instrument: { title: "Set instrument", description: "Choose a synth track's instrument.", input: { track, instrument: z.enum(["nebula_poly", "nebula_bass"]) } },
  set_instrument_param: {
    title: "Set instrument parameter",
    description: "Set one parameter of a synth track's current instrument (ids and ranges from list_instruments; wave also accepts sine/saw/square/triangle).",
    input: { track, param: z.string().min(1).max(32), value: z.union([z.number(), z.enum(["sine", "saw", "square", "triangle"])]) },
  },

  // --- export ------------------------------------------------------------------------------
  export_wav: {
    title: "Export WAV",
    description: "Render the song (bar 1 to the last clip, plus release tails) or the loop region to a WAV file in the background. "
      + "Returns a job id: poll get_export_status until state is done/failed/cancelled. The file is validated after writing.",
    input: {
      file: file(".wav"),
      range: z.enum(["song", "loop"]).optional().describe("Default song"),
      sample_rate: z.union([z.literal(44100), z.literal(48000), z.literal(96000)]).optional().describe("Default 48000"),
      bit_depth: z.union([z.literal(16), z.literal(24), z.literal(32)]).optional().describe("Default 24; 32 = float"),
      overwrite,
    },
  },
  get_export_status: {
    title: "Get export status",
    description: "Progress and result of an export job: state running/done/failed/cancelled, length, format, peak level.",
    input: { job: z.string().min(1).max(32) },
  },
  cancel_export: { title: "Cancel export", description: "Cancel a running export; no file is left behind.", input: { job: z.string().min(1).max(32) } },
  export_midi: {
    title: "Export MIDI",
    description: "Write the song as a Standard MIDI File (tempo, time signature, one track per audible track, drums on channel 10).",
    input: { file: file(".mid"), overwrite },
  },
};

export const toolNames = Object.keys(tools);

function text(value: unknown) {
  return { content: [{ type: "text" as const, text: typeof value === "string" ? value : JSON.stringify(value, null, 2) }] };
}

function failure(err: unknown) {
  const e = err instanceof DawError ? err : new DawError("error", err instanceof Error ? err.message : String(err));
  return { ...text(`${e.code}: ${e.message}`), isError: true };
}

export function registerTools(server: McpServer, client: BeatForgeClient): void {
  for (const [name, def] of Object.entries(tools)) {
    server.registerTool(name, { title: def.title, description: def.description, inputSchema: def.input ?? {} },
      async (args: Record<string, unknown>) => {
        try {
          return text(await client.request(name, args ?? {}));
        } catch (err) {
          return failure(err);
        }
      });
  }

  server.registerTool("get_connection_status", {
    title: "Connection status",
    description: "Whether BeatForge is running and connected: app version, socket path, the folder files may be saved to.",
    inputSchema: {},
  }, async () => {
    try {
      const hello = await client.request("hello", {}, 3000) as Record<string, unknown>;
      return text({ connected: true, socket: client.socketPath, app: hello.app, version: hello.version, allowed_folders: hello.allowed_folders });
    } catch (err) {
      const e = err instanceof DawError ? err : new DawError("error", String(err));
      return text({ connected: false, socket: client.socketPath, reason: `${e.code}: ${e.message}` });
    }
  });
}
