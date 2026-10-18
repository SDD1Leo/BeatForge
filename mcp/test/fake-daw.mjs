// A stand-in for the BeatForge app's socket: answers with a handler, records requests.
import net from "node:net";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

export async function startFakeDaw(handler) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "bf-mcp-"));
  // Windows: a named pipe, as the app uses there.
  const socketPath = process.platform === "win32"
    ? `\\\\.\\pipe\\${path.basename(dir)}`
    : path.join(dir, "daw.sock");
  const requests = [];
  const sockets = new Set();
  const server = net.createServer((socket) => {
    sockets.add(socket);
    socket.on("close", () => sockets.delete(socket));
    let buffer = "";
    socket.setEncoding("utf8");
    socket.on("data", async (chunk) => {
      buffer += chunk;
      for (let nl = buffer.indexOf("\n"); nl >= 0; nl = buffer.indexOf("\n")) {
        const req = JSON.parse(buffer.slice(0, nl));
        buffer = buffer.slice(nl + 1);
        requests.push(req);
        const reply = await handler(req, socket);
        if (reply !== undefined) socket.write(JSON.stringify({ id: req.id, ...reply }) + "\n");
      }
    });
  });
  await new Promise((r) => server.listen(socketPath, r));
  return {
    socketPath, requests,
    dropClients: () => { for (const s of sockets) s.destroy(); },
    close: () => new Promise((r) => { for (const s of sockets) s.destroy(); server.close(() => { fs.rmSync(dir, { recursive: true, force: true }); r(); }); }),
  };
}
