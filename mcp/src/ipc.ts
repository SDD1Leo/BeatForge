// Client side of BeatForge's local IPC: a Unix domain socket, one JSON
// request/response per line. See core/include/beatforge/ipc/IpcServer.h.
import net from "node:net";
import os from "node:os";
import path from "node:path";

export function defaultSocketPath(): string {
  if (process.env.BEATFORGE_SOCKET) return process.env.BEATFORGE_SOCKET;
  if (process.env.XDG_RUNTIME_DIR) return path.join(process.env.XDG_RUNTIME_DIR, "beatforge", "daw.sock");
  return path.join("/tmp", `beatforge-${os.userInfo().uid}`, "daw.sock");
}

/** An error reported by BeatForge (or by the connection), with a stable code. */
export class DawError extends Error {
  constructor(public readonly code: string, message: string) {
    super(message);
  }
}

type Pending = { resolve: (v: unknown) => void; reject: (e: Error) => void; timer: NodeJS.Timeout };

export class BeatForgeClient {
  private socket?: net.Socket;
  private connecting?: Promise<void>;
  private buffer = "";
  private pending = new Map<number, Pending>();
  private nextId = 1;

  constructor(readonly socketPath = defaultSocketPath(), readonly timeoutMs = 15000) {}

  get connected(): boolean {
    return this.socket !== undefined && !this.socket.destroyed;
  }

  /** Sends one operation; resolves with its result or rejects with a DawError. */
  async request(op: string, args: Record<string, unknown> = {}, timeoutMs = this.timeoutMs): Promise<unknown> {
    await this.connect();
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);   // a late answer is ignored
        reject(new DawError("timeout", `BeatForge did not answer '${op}' within ${timeoutMs / 1000} s`));
      }, timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
      this.socket!.write(JSON.stringify({ id, op, args }) + "\n");
    });
  }

  close(): void {
    this.socket?.destroy();
    this.socket = undefined;
  }

  private connect(): Promise<void> {
    if (this.connected) return Promise.resolve();
    if (this.connecting) return this.connecting;
    this.connecting = new Promise<void>((resolve, reject) => {
      const socket = net.createConnection({ path: this.socketPath });
      socket.setEncoding("utf8");
      socket.once("connect", () => {
        this.socket = socket;
        this.connecting = undefined;
        resolve();
      });
      socket.on("data", (chunk: string) => this.onData(chunk));
      socket.on("error", (err: NodeJS.ErrnoException) => {
        if (this.socket !== socket) {
          this.connecting = undefined;
          const missing = err.code === "ENOENT" || err.code === "ECONNREFUSED";
          reject(new DawError("not_running", missing
            ? `BeatForge is not running (nothing listens on ${this.socketPath}). Start BeatForge and try again.`
            : `cannot connect to BeatForge at ${this.socketPath}: ${err.message}`));
        }
      });
      socket.on("close", () => {
        if (this.socket === socket) this.socket = undefined;
        this.failAll(new DawError("disconnected", "BeatForge closed the connection (did the app quit?)"));
      });
    });
    return this.connecting;
  }

  private onData(chunk: string): void {
    this.buffer += chunk;
    for (let nl = this.buffer.indexOf("\n"); nl >= 0; nl = this.buffer.indexOf("\n")) {
      const line = this.buffer.slice(0, nl);
      this.buffer = this.buffer.slice(nl + 1);
      let msg: { id?: number; ok?: boolean; result?: unknown; error?: { code?: string; message?: string } };
      try {
        msg = JSON.parse(line);
      } catch {
        continue;
      }
      const p = msg.id !== undefined ? this.pending.get(msg.id) : undefined;
      if (!p) continue;
      this.pending.delete(msg.id!);
      clearTimeout(p.timer);
      if (msg.ok) p.resolve(msg.result);
      else p.reject(new DawError(msg.error?.code ?? "error", msg.error?.message ?? "unknown error"));
    }
  }

  private failAll(err: DawError): void {
    for (const [id, p] of this.pending) {
      clearTimeout(p.timer);
      p.reject(err);
      this.pending.delete(id);
    }
  }
}
