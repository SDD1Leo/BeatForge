#!/usr/bin/env node
// End-to-end check against a RUNNING BeatForge: the Phase 10 scenario through
// the real MCP server (dist/index.js) and the real app. Edits the open project!
// Usage: node scripts/e2e.mjs [--play-seconds=4]
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

const playSeconds = Number((process.argv.find((a) => a.startsWith("--play-seconds=")) ?? "=4").split("=")[1]);
const client = new Client({ name: "beatforge-e2e", version: "1" });
await client.connect(new StdioClientTransport({
  command: process.execPath, args: [new URL("../dist/index.js", import.meta.url).pathname], env: process.env, stderr: "inherit",
}));

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function call(name, args = {}) {
  const r = await client.callTool({ name, arguments: args });
  const text = r.content[0].text;
  console.log(`\n> ${name} ${JSON.stringify(args)}${r.isError ? "  [ERROR]" : ""}\n${text.length > 600 ? text.slice(0, 600) + " ..." : text}`);
  return { error: r.isError === true, value: r.isError ? text : JSON.parse(text) };
}

const status = await call("get_connection_status");
if (!status.value.connected) process.exit(2);

await call("get_project_state");                                                            // 1
await call("set_tempo", { bpm: 128 });                                                      // 2
const track = (await call("create_track", { type: "synth", name: "MCP Lead", instrument: "nebula_poly" })).value;   // 3
const clip = (await call("create_clip", { track: track.id, bar: 1, bars: 2 })).value;       // 4
await call("add_midi_notes", { clip: clip.id, notes: [                                      // 5
  { pitch: "C4", start_beat: 0, length_beats: 1, velocity: 110 },
  { pitch: "E4", start_beat: 1, length_beats: 1 },
  { pitch: "G4", start_beat: 2, length_beats: 1 },
  { pitch: "C5", start_beat: 3, length_beats: 1 },
  { pitch: "G4", start_beat: 4, length_beats: 1 },
  { pitch: "E4", start_beat: 5, length_beats: 1 },
  { pitch: "C4", start_beat: 6, length_beats: 2 } ] });
await call("seek", { bar: 1 });
await call("play");                                                                         // 6
await sleep(playSeconds * 1000);
await call("get_transport_state");
await call("stop");
const job = (await call("export_wav", { file: "mcp-e2e", sample_rate: 48000, bit_depth: 24, overwrite: true })).value;   // 7
let s;
do { await sleep(200); s = (await call("get_export_status", { job: job.job })).value; } while (s.state === "running");
console.log(`\nEXPORT ${s.state.toUpperCase()}: ${s.file} ${s.seconds?.toFixed?.(3)} s, peak ${s.peak_dbfs?.toFixed?.(1)} dBFS`);   // 8

// Errors come back as errors.
await call("set_tempo", { bpm: 999 });
await call("export_wav", { file: "/etc/evil" });
await call("create_project", {});
await client.close();
process.exit(s.state === "done" ? 0 : 1);
