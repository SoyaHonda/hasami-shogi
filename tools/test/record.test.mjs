// Game records of the simulator (web/record.js): a game played through a Recorder replays on a
// new driver to the same state, also after a JSON round trip, with uneven frame times, settings
// changed and the game paused on the way.
import { test, before } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadHasami } from '../load-wasm.mjs';
import { DT_CHARS, FORMAT, Recorder, enumName, recordPath, replay, resultOf, uploadRecord } from '../../web/record.js';

let m;
before(async () => {
  m = await loadHasami();
});

// xorshift32, so that the test plays the same games every time.
function rng(seed) {
  let x = seed >>> 0 || 1;
  return () => {
    x ^= x << 13;
    x >>>= 0;
    x ^= x >>> 17;
    x ^= x << 5;
    x >>>= 0;
    return x / 4294967296;
  };
}

// The person's move as the page makes it: lift a cooled-down human piece, put it on a square in
// its row or column (back on its origin when that is refused).
function personMove(sim, rec, rand) {
  const s = sim.state();
  if (s.pieces.some((p) => p.held && p.side === 1)) return;
  const mine = s.pieces.filter((p) => p.alive && !p.held && p.side === 1 && p.cooldownLeft === 0);
  if (!mine.length) return;
  const p = mine[Math.floor(rand() * mine.length)];
  const lr = sim.lift(p.r, p.c);
  rec.lift(p.r, p.c, lr);
  if (lr !== m.LiftResult.Ok) return;
  const n = s.n;
  const along = rand() < 0.5;
  const k = Math.floor(rand() * n);
  const [r, c] = along ? [p.r, k] : [k, p.c];
  const res = sim.place(r, c);
  rec.place(r, c, res);
  if (res === m.PlaceResult.Illegal) rec.place(p.r, p.c, sim.place(p.r, p.c));
}

// Plays a game as the page does (a new driver, frames of 16/17 ms and some longer ones) and returns
// the record and the final state.
function playGame(seed, { maxFrames = 40000, changes = true } = {}) {
  const rules = m.officialRules();
  const params = { ...m.defaults().params, level: m.Level.Hard };
  const sim = new m.SimDriver(rules, params, seed);
  const rec = new Recorder(m, { rules, params, seed, date: '2026-11-16T00:00:00.000Z', id: `t${seed}` });
  sim.start();
  const rand = rng(seed * 31);
  let paused = false;
  for (let f = 0; f < maxFrames; f++) {
    const dt = f % 97 === 50 ? 50 : 16 + (f % 3 === 0 ? 1 : 0);
    sim.advance(dt);
    rec.advance(dt);
    if (sim.phase() === m.Phase.Over) break;
    if (changes && f === 900) {
      const p = { ...params, aggression: 0.8, level: m.Level.Normal };
      sim.setParams(p);
      rec.setParams(p);
    }
    if (changes && (f === 1500 || f === 1700)) {
      sim.togglePause();
      rec.togglePause();
      paused = !paused;
    }
    if (!paused && rand() < 0.02) personMove(sim, rec, rand);
  }
  const state = sim.state();
  const done = rec.finish(sim.phase() === m.Phase.Over ? 'over' : 'abandoned', state);
  sim.delete();
  return { rec: done, state };
}

test('a recorded game replays to the same state', () => {
  const { rec, state } = playGame(5);
  assert.equal(rec.format, FORMAT);
  assert.equal(rec.end, 'over');
  assert.match(rec.build.coreHash, /^[0-9a-f]{16}$/);
  assert.ok(rec.build.commit.length > 0);
  assert.ok(rec.ops.some((o) => o[0] === 'L') && rec.ops.some((o) => o[0] === 'S') && rec.ops.some((o) => o[0] === 'Z'));
  const lifts = [];
  const sim = replay(m, JSON.parse(JSON.stringify(rec)), (op, s) => {
    if (op[0] === 'L') lifts.push(s.clockMs());
  });
  try {
    assert.deepEqual(resultOf(m, sim), rec.result);
    const got = sim.state();
    assert.deepEqual(Array.from(got.cells), Array.from(state.cells));
    assert.deepEqual(
      got.pieces.map((p) => [p.id, p.r, p.c, p.alive]),
      state.pieces.map((p) => [p.id, p.r, p.c, p.alive]),
    );
    assert.equal(got.clockMs, state.clockMs);
    assert.equal(lifts.length, rec.ops.filter((o) => o[0] === 'L').length);
  } finally {
    sim.delete();
  }
});

test('several games, each from its own driver, all replay', () => {
  for (const seed of [11, 12, 13]) {
    const { rec } = playGame(seed, { changes: false });
    const sim = replay(m, rec);
    try {
      assert.deepEqual(resultOf(m, sim), rec.result, `seed ${seed}`);
    } finally {
      sim.delete();
    }
  }
});

test('a game cut short replays to where it was cut', () => {
  const { rec } = playGame(21, { maxFrames: 3000 });
  assert.equal(rec.end, 'abandoned');
  assert.equal(rec.result.phase === 'Over', false);
  const sim = replay(m, rec);
  try {
    assert.deepEqual(resultOf(m, sim), rec.result);
  } finally {
    sim.delete();
  }
});

test('frame times are one character each; longer ones go as numbers', () => {
  const rec = new Recorder(m, { rules: m.officialRules(), params: m.defaults().params, seed: 1, id: 'x' });
  rec.advance(1);
  rec.advance(DT_CHARS.length);
  rec.advance(DT_CHARS.length + 1);
  rec.advance(16);
  rec.lift(6, 0, m.LiftResult.Ok);
  const out = rec.finish('abandoned', { phase: m.Phase.Ready, winner: m.Winner.None, capturedHuman: 0, capturedMachine: 0, clockMs: 0, saves: 0, machineMoves: 0 });
  assert.deepEqual(out.ops, [DT_CHARS[0] + DT_CHARS[DT_CHARS.length - 1], [DT_CHARS.length + 1], DT_CHARS[15], ['L', 6, 0, 'Ok']]);
  assert.equal(out.params.level, enumName(m.Level, m.defaults().params.level));
  assert.ok(rec.touched);
});

test('tools/replay.mjs checks records and writes the moves', () => {
  const dir = mkdtempSync(join(tmpdir(), 'hasami-rec-'));
  const games = join(dir, 'games.jsonl'), moves = join(dir, 'moves.jsonl');
  const recs = [playGame(31, { changes: false }).rec, playGame(32, { maxFrames: 2000 }).rec];
  writeFileSync(games, recs.map((r) => JSON.stringify(r)).join('\n') + '\n');
  const tool = fileURLToPath(new URL('../replay.mjs', import.meta.url));
  const ok = spawnSync(process.execPath, [tool, games, '--moves', moves], { encoding: 'utf8' });
  assert.equal(ok.status, 0, ok.stdout + ok.stderr);
  assert.match(ok.stdout, /2 records \(1 finished\): 2 replay, 0 do not/);
  const lines = readFileSync(moves, 'utf8').trim().split('\n').map((l) => JSON.parse(l));
  assert.ok(lines.length > 5);
  for (const x of lines) {
    assert.ok(x.position.pieces.length > 0 && Array.isArray(x.lift));
    const p = x.position.pieces.find((q) => q.alive && !q.held && q.r === x.lift[0] && q.c === x.lift[1]);
    assert.ok(p && p.side === 1, 'the lifted square held a piece of the person');
  }
  // A changed record no longer replays.
  const broken = { ...recs[0], result: { ...recs[0].result, clockMs: recs[0].result.clockMs + 1 } };
  writeFileSync(games, JSON.stringify(broken) + '\n');
  const ng = spawnSync(process.execPath, [tool, games], { encoding: 'utf8' });
  assert.equal(ng.status, 1);
  assert.match(ng.stdout, /does not replay/);
});

test('uploadRecord puts one file per game with the token, and says what went wrong', async () => {
  const rec = { format: 1, id: 'ab12', date: '2026-11-16T06:20:23.123Z', ops: ['AB'], result: {} };
  assert.equal(recordPath(rec, 'games'), 'games/2026-11-16/2026-11-16T06-20-23-123Z-ab12.json');
  assert.equal(recordPath(rec, '/data/games/'), 'data/games/2026-11-16/2026-11-16T06-20-23-123Z-ab12.json');
  const calls = [];
  const reply = (status, body = {}) => async (url, init) => {
    calls.push({ url, init });
    return { status, json: async () => body };
  };
  const cfg = { repo: 'me/hasami-games', token: 'tok', branch: 'main', dir: 'games' };
  const ok = await uploadRecord(rec, cfg, reply(201));
  assert.equal(ok.ok, true);
  const { url, init } = calls[0];
  assert.equal(url, 'https://api.github.com/repos/me/hasami-games/contents/games/2026-11-16/2026-11-16T06-20-23-123Z-ab12.json');
  assert.equal(init.method, 'PUT');
  assert.equal(init.headers.Authorization, 'Bearer tok');
  const body = JSON.parse(init.body);
  assert.match(body.message, /\[skip ci\]/);
  assert.equal(body.branch, 'main');
  assert.deepEqual(JSON.parse(Buffer.from(body.content, 'base64').toString('utf8')), rec);
  // Already there: sent before.
  assert.equal((await uploadRecord(rec, cfg, reply(422, { message: '"sha" wasn\'t supplied.' }))).ok, true);
  const bad = await uploadRecord(rec, cfg, reply(401, { message: 'Bad credentials' }));
  assert.equal(bad.ok, false);
  assert.match(bad.message, /トークン/);
  assert.equal((await uploadRecord(rec, { ...cfg, repo: 'no slash' }, reply(201))).ok, false);
  const down = await uploadRecord(rec, cfg, async () => {
    throw new Error('offline');
  });
  assert.equal(down.ok, false);
  assert.match(down.message, /offline/);
});
