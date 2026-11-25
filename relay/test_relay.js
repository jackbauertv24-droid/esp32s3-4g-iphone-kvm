'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const net = require('node:net');
const { createRelay } = require('./server');

test('4G reverse-tunnel relay rejects bad tokens and bridges sealed CLI frames end-to-end', async () => {
  const relay = createRelay({ token: 'unit-test-secret', devicePort: 0, clientPort: 0, httpPort: 0 });
  const ports = await relay.start();

  try {
    // 1. Unauthorized modem must be rejected
    await new Promise((resolve) => {
      const bad = net.connect(ports.devicePort, '127.0.0.1', () => {
        bad.write('REG 246f28aa6ad5 wrong-token\n');
      });
      let resp = '';
      bad.on('data', (d) => { resp += d.toString(); });
      bad.on('close', () => {
        assert.match(resp, /ERR unauthorized/);
        resolve();
      });
    });

    // 2. Authorized A7670E modem connects outbound, then CLI exchanges binary [u16 len][frame] records
    const boardRx = [];
    const board = net.connect(ports.devicePort, '127.0.0.1');
    await new Promise((resolve) => board.once('connect', resolve));
    board.write('REG 246f28aa6ad5 unit-test-secret\n');

    // Wait briefly for registration processing
    await new Promise((r) => setTimeout(r, 40));

    const statusResp = await fetch(`http://127.0.0.1:${ports.httpPort}/api/status`).then((r) => r.json());
    assert.equal(statusResp.boardOnline, true);
    assert.equal(statusResp.boardId, '246f28aa6ad5');

    const cli = net.connect(ports.clientPort, '127.0.0.1');
    await new Promise((resolve) => cli.once('connect', resolve));

    const frameFromCli = Buffer.from([0x04, 0x00, 0x10, 0xde, 0xad, 0xbe]);
    const frameFromBoard = Buffer.from([0x03, 0x00, 0x82, 0xca, 0xfe]);

    const gotAtBoard = new Promise((resolve) => board.once('data', resolve));
    cli.write(frameFromCli);
    assert.deepEqual(await gotAtBoard, frameFromCli);

    const gotAtCli = new Promise((resolve) => cli.once('data', resolve));
    board.write(frameFromBoard);
    assert.deepEqual(await gotAtCli, frameFromBoard);

    cli.destroy();
    board.destroy();
  } finally {
    await relay.stop();
  }
});
