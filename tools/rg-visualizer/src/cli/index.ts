#!/usr/bin/env node
/** CLI entry: watch a JSON file and push updates via WebSocket to the browser. */

import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { WebSocketServer, type WebSocket } from 'ws';
import { watch } from 'chokidar';

const args = process.argv.slice(2);
const filePath = args[0];
const port = Number(args[1] ?? '9221');

if (!filePath) {
  console.error('Usage: tsx src/cli/index.ts <json-file> [port]');
  process.exit(1);
}

const resolvedPath = resolve(filePath);
const clients = new Set<WebSocket>();

const httpServer = createServer((_req, res) => {
  res.writeHead(200, { 'Content-Type': 'text/plain' });
  res.end('miki rg-visualizer ws server\n');
});

const wss = new WebSocketServer({ server: httpServer });

wss.on('connection', (ws) => {
  clients.add(ws);
  console.log(`[ws] client connected (${clients.size} total)`);
  // Send current file content immediately
  try {
    const data = readFileSync(resolvedPath, 'utf-8');
    ws.send(data);
  } catch { /* file may not exist yet */ }
  ws.on('close', () => {
    clients.delete(ws);
    console.log(`[ws] client disconnected (${clients.size} total)`);
  });
});

function broadcast() {
  try {
    const data = readFileSync(resolvedPath, 'utf-8');
    for (const client of clients) {
      if (client.readyState === 1) client.send(data);
    }
    console.log(`[ws] broadcast to ${clients.size} clients`);
  } catch (e) {
    console.error('[ws] failed to read file:', e);
  }
}

const watcher = watch(resolvedPath, { persistent: true, ignoreInitial: true });
watcher.on('change', () => {
  console.log(`[fs] ${resolvedPath} changed`);
  broadcast();
});

httpServer.listen(port, () => {
  console.log(`[ws] listening on ws://localhost:${port}`);
  console.log(`[fs] watching ${resolvedPath}`);
});
