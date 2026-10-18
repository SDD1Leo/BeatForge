import { test } from "node:test";
import assert from "node:assert/strict";
import { BeatForgeClient, DawError } from "../dist/ipc.js";
import { startFakeDaw } from "./fake-daw.mjs";

test("results and app errors come back with their codes", async () => {
  const daw = await startFakeDaw((req) => req.op === "get_tempo"
    ? { ok: true, result: { bpm: 120 } }
    : { ok: false, error: { code: "invalid_argument", message: "'bpm' must be between 40 and 240" } });
  const client = new BeatForgeClient(daw.socketPath, 2000);
  assert.deepEqual(await client.request("get_tempo"), { bpm: 120 });
  await assert.rejects(client.request("set_tempo", { bpm: 999 }), (e) => e instanceof DawError && e.code === "invalid_argument");
  assert.deepEqual(daw.requests.map((r) => r.op), ["get_tempo", "set_tempo"]);
  assert.deepEqual(daw.requests[1].args, { bpm: 999 });
  client.close();
  await daw.close();
});

test("a request the app never answers times out", async () => {
  const daw = await startFakeDaw(() => undefined);
  const client = new BeatForgeClient(daw.socketPath, 300);
  const started = Date.now();
  await assert.rejects(client.request("play"), (e) => e.code === "timeout");
  assert.ok(Date.now() - started < 2000);
  client.close();
  await daw.close();
});

const missingEndpoint = process.platform === "win32"
  ? "\\\\.\\pipe\\definitely-not-beatforge" : "/tmp/definitely-not-beatforge/daw.sock";

test("no app running: a clear not_running error", async () => {
  const client = new BeatForgeClient(missingEndpoint, 1000);
  await assert.rejects(client.request("hello"), (e) => e.code === "not_running" && /Start BeatForge/.test(e.message));
});

test("the app quitting mid-request rejects it, and the client reconnects later", async () => {
  let answer = false;
  const daw = await startFakeDaw(() => (answer ? { ok: true, result: "back" } : undefined));
  const client = new BeatForgeClient(daw.socketPath, 5000);
  const pending = client.request("export_wav", { file: "x" });
  setTimeout(() => daw.dropClients(), 100);
  await assert.rejects(pending, (e) => e.code === "disconnected");
  answer = true;
  assert.equal(await client.request("hello"), "back");
  client.close();
  await daw.close();
});
