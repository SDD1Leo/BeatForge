import { test } from "node:test";
import assert from "node:assert/strict";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { fileURLToPath } from "node:url";
import { startFakeDaw } from "./fake-daw.mjs";

async function connect(socketPath) {
  const client = new Client({ name: "test", version: "1" });
  await client.connect(new StdioClientTransport({
    command: process.execPath, args: [fileURLToPath(new URL("../dist/index.js", import.meta.url))],
    env: { ...process.env, BEATFORGE_SOCKET: socketPath }, stderr: "ignore",
  }));
  return client;
}

test("only implemented tools are offered", async () => {
  const daw = await startFakeDaw(() => ({ ok: true, result: {} }));
  const client = await connect(daw.socketPath);
  const names = (await client.listTools()).tools.map((t) => t.name);
  for (const required of ["get_project_state", "set_tempo", "create_track", "create_clip", "add_midi_notes", "play", "export_wav", "get_export_status", "cancel_export", "get_connection_status"])
    assert.ok(names.includes(required), required);
  for (const missing of ["pause", "add_effect", "list_supported_effects", "remove_effect", "run_command"])
    assert.ok(!names.includes(missing), missing);
  await client.close();
  await daw.close();
});

test("invalid arguments are rejected before reaching the app", async () => {
  const daw = await startFakeDaw(() => ({ ok: true, result: {} }));
  const client = await connect(daw.socketPath);
  const bad = [
    ["set_tempo", { bpm: 999 }],
    ["set_tempo", { bpm: "fast" }],
    ["create_track", { type: "guitar" }],
    ["add_midi_notes", { clip: 3, notes: [{ pitch: "H9", start_beat: 0, length_beats: 1 }] }],
    ["set_drum_pattern", { clip: 1, pattern: { kick: "x..x" } }],
    ["export_wav", { file: "a", sample_rate: 22050 }],
  ];
  for (const [name, args] of bad) {
    const r = await client.callTool({ name, arguments: args }).catch((e) => ({ isError: true, content: [{ text: String(e) }] }));
    assert.equal(r.isError, true, `${name} ${JSON.stringify(args)}`);
  }
  assert.equal(daw.requests.length, 0);
  await client.close();
  await daw.close();
});

test("valid calls reach the app unchanged; app errors are tool errors with their code", async () => {
  const daw = await startFakeDaw((req) => req.op === "set_tempo"
    ? { ok: true, result: { previous_bpm: 120, bpm: req.args.bpm } }
    : { ok: false, error: { code: "confirmation_required", message: "unsaved changes would be lost" } });
  const client = await connect(daw.socketPath);
  const ok = await client.callTool({ name: "set_tempo", arguments: { bpm: 128 } });
  assert.ok(!ok.isError);
  assert.deepEqual(JSON.parse(ok.content[0].text), { previous_bpm: 120, bpm: 128 });
  assert.deepEqual(daw.requests[0], { id: 1, op: "set_tempo", args: { bpm: 128 } });

  const refused = await client.callTool({ name: "create_project", arguments: {} });
  assert.equal(refused.isError, true);
  assert.match(refused.content[0].text, /^confirmation_required:/);
  await client.close();
  await daw.close();
});

test("connection status reports a missing app without failing", async () => {
  const client = await connect(process.platform === "win32" ? "\\\\.\\pipe\\nothing-here" : "/tmp/nothing-here/daw.sock");
  const r = await client.callTool({ name: "get_connection_status", arguments: {} });
  const status = JSON.parse(r.content[0].text);
  assert.equal(status.connected, false);
  assert.match(status.reason, /not_running/);
  const play = await client.callTool({ name: "play", arguments: {} });
  assert.equal(play.isError, true);
  assert.match(play.content[0].text, /Start BeatForge/);
  await client.close();
});
