#!/usr/bin/env node
/**
 * 4G LTE Reverse-Tunnel Relay for Waveshare ESP32-S3-A7670E-4G iPhone KVM
 *
 * Solves cellular CGNAT: the ESP32-S3-A7670E-4G dials OUT over 4G (`AT+CIPOPEN`) to `DEVICE_PORT` (9001)
 * and authenticates with `REG <board_mac_hex> <token>\n`.
 * Your `esp32agent` CLI / MCP server connects to `CLIENT_PORT` (7202), and end-to-end X25519 + AES-128-GCM
 * `[u16 LE length][frame]` records are bridged zero-trust between the client and the 4G board!
 */
'use strict';

const net = require('node:net');
const http = require('node:http');
const crypto = require('node:crypto');

function safeTokenMatch(expected, actual) {
  const a = Buffer.from(String(expected || ''));
  const b = Buffer.from(String(actual || ''));
  if (a.length === 0 || a.length !== b.length) return false;
  return crypto.timingSafeEqual(a, b);
}

function createRelay(opts = {}) {
  const token = opts.token || process.env.RELAY_TOKEN || 'changeme-in-prod';
  const devicePort = opts.devicePort ?? Number(process.env.DEVICE_PORT || 9001);
  const clientPort = opts.clientPort ?? Number(process.env.CLIENT_PORT || 7202);
  const httpPort = opts.httpPort ?? Number(process.env.HTTP_PORT || 8080);

  let activeBoard = null;   // { id, socket, connectedAt, rxBytes, txBytes, pendingBuf }
  let activeClient = null;  // net.Socket

  const deviceServer = net.createServer((sock) => {
    sock.setNoDelay(true);
    sock.setKeepAlive(true, 15000);

    let registered = false;
    let headerBuf = Buffer.alloc(0);

    const authTimer = setTimeout(() => {
      if (!registered) sock.destroy();
    }, 8000);
    authTimer.unref();

    sock.on('data', (chunk) => {
      if (!registered) {
        headerBuf = Buffer.concat([headerBuf, chunk]);
        const nl = headerBuf.indexOf(0x0a); // '\n'
        if (nl < 0) {
          if (headerBuf.length > 256) sock.destroy();
          return;
        }
        const line = headerBuf.subarray(0, nl).toString('utf8').trim();
        const rest = headerBuf.subarray(nl + 1);
        const parts = line.split(/\s+/);
        if (parts.length !== 3 || parts[0] !== 'REG' || !/^[0-9a-fA-F]{12}$/.test(parts[1]) ||
            !safeTokenMatch(token, parts[2])) {
          sock.write('ERR unauthorized\n');
          sock.destroy();
          return;
        }

        registered = true;
        clearTimeout(authTimer);
        if (activeBoard && activeBoard.socket !== sock) {
          activeBoard.socket.destroy();
        }
        activeBoard = {
          id: parts[1].toLowerCase(),
          socket: sock,
          connectedAt: Date.now(),
          rxBytes: rest.length,
          txBytes: 0,
        };
        if (rest.length > 0 && activeClient && !activeClient.destroyed) {
          activeClient.write(rest);
        }
        return;
      }

      if (activeBoard && activeBoard.socket === sock) {
        activeBoard.rxBytes += chunk.length;
        if (activeClient && !activeClient.destroyed) {
          activeClient.write(chunk);
        }
      }
    });

    const cleanup = () => {
      if (activeBoard && activeBoard.socket === sock) {
        activeBoard = null;
        if (activeClient && !activeClient.destroyed) activeClient.destroy();
      }
    };
    sock.on('close', cleanup);
    sock.on('error', cleanup);
  });

  const clientServer = net.createServer((clientSock) => {
    clientSock.setNoDelay(true);
    if (!activeBoard || activeBoard.socket.destroyed) {
      clientSock.destroy();
      return;
    }
    if (activeClient && !activeClient.destroyed) {
      activeClient.destroy();
    }
    activeClient = clientSock;

    clientSock.on('data', (chunk) => {
      if (activeBoard && !activeBoard.socket.destroyed) {
        activeBoard.txBytes += chunk.length;
        activeBoard.socket.write(chunk);
      } else {
        clientSock.destroy();
      }
    });

    const cleanupClient = () => {
      if (activeClient === clientSock) activeClient = null;
    };
    clientSock.on('close', cleanupClient);
    clientSock.on('error', cleanupClient);
  });

  const httpServer = http.createServer((req, res) => {
    if (req.url === '/api/status') {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({
        boardOnline: Boolean(activeBoard),
        boardId: activeBoard ? activeBoard.id : null,
        uptimeSec: activeBoard ? Math.floor((Date.now() - activeBoard.connectedAt) / 1000) : 0,
        rxBytes: activeBoard ? activeBoard.rxBytes : 0,
        txBytes: activeBoard ? activeBoard.txBytes : 0,
        cliConnected: Boolean(activeClient && !activeClient.destroyed),
      }));
      return;
    }
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end(`<!doctype html><html><head><meta charset="utf-8"><title>ESP32-S3 4G iPhone KVM Relay</title>
<style>body{font-family:system-ui,sans-serif;max-width:680px;margin:2rem auto;padding:0 1rem;background:#0f172a;color:#e2e8f0}
.card{background:#1e293b;padding:1.25rem;border-radius:10px;border:1px solid #334155;margin-top:1rem}
code{background:#0f172a;padding:2px 6px;border-radius:4px;color:#38bdf8}</style></head>
<body><h2>ESP32-S3-A7670E-4G iPhone KVM Relay</h2>
<div class="card" id="st">Loading tunnel status...</div>
<div class="card"><h3>Quick Connect</h3>
<p>1. On the ESP32-S3 serial/BLE console: <code>0 cell tunnel &lt;relay-host&gt; ${devicePort} &lt;token&gt;</code></p>
<p>2. Point your paired <code>esp32agent</code> CLI / MCP client at TCP port <code>${clientPort}</code> (end-to-end X25519 + AES-128-GCM sealed).</p></div>
<script>async function poll(){const r=await fetch('/api/status').then(x=>x.json());
document.getElementById('st').innerHTML='<b>4G Board:</b> '+(r.boardOnline?'🟢 Online ('+r.boardId+')':'⚪ Waiting for A7670E connection')+
'<br><b>CLI Session:</b> '+(r.cliConnected?'🟢 Connected':'⚪ Idle')+
'<br><b>Relayed:</b> ↓ '+r.rxBytes+' B / ↑ '+r.txBytes+' B (up '+r.uptimeSec+'s)';}
poll();setInterval(poll,2000);</script></body></html>`);
  });

  return {
    start() {
      return Promise.all([
        new Promise((r) => deviceServer.listen(devicePort, r)),
        new Promise((r) => clientServer.listen(clientPort, r)),
        new Promise((r) => httpServer.listen(httpPort, r)),
      ]).then(() => ({
        devicePort: deviceServer.address().port,
        clientPort: clientServer.address().port,
        httpPort: httpServer.address().port,
      }));
    },
    stop() {
      if (activeBoard) activeBoard.socket.destroy();
      if (activeClient) activeClient.destroy();
      return Promise.all([
        new Promise((r) => deviceServer.close(() => r())),
        new Promise((r) => clientServer.close(() => r())),
        new Promise((r) => httpServer.close(() => r())),
      ]);
    },
  };
}

if (require.main === module) {
  const relay = createRelay();
  relay.start().then((ports) => {
    console.log(`[4g-kvm-relay] A7670E tunnel port :${ports.devicePort} | CLI port :${ports.clientPort} | Web status :${ports.httpPort}`);
  });
}

module.exports = { createRelay };
