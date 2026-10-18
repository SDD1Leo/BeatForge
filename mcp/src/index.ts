#!/usr/bin/env node
// BeatForge MCP server (stdio). Talks to the running BeatForge app over a
// local Unix socket; it never opens a network port and never runs shell
// commands. Logs go to stderr (stdout carries the MCP protocol).
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { BeatForgeClient } from "./ipc.js";
import { registerTools } from "./tools.js";

const server = new McpServer(
  { name: "beatforge", version: "0.10.0" },
  {
    instructions:
      "BeatForge is a desktop DAW running on this computer. Use get_project_state first. Bars and beats are 1-based "
      + "except note positions inside a clip, which count beats from 0. Edits change the real, open project and can be "
      + "undone with undo. Never pass confirm or overwrite = true unless the user explicitly agreed. Exports run in the "
      + "background: poll get_export_status with the returned job id.",
  },
);

registerTools(server, new BeatForgeClient());
await server.connect(new StdioServerTransport());
process.stderr.write("beatforge-mcp: ready\n");
