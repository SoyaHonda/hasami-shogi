import { test, before } from 'node:test';
import assert from 'node:assert/strict';
import { loadHasami } from '../load-wasm.mjs';

let m;
before(async () => {
  m = await loadHasami();
});

// Runs `fn` with a new driver and frees it afterwards.
function withSim(args, fn) {
  const sim = new m.SimDriver(...args);
  try {
    return fn(sim);
  } finally {
    sim.delete();
  }
}

// Advances through the 3 s start countdown into Play.
function startPlay(sim) {
  sim.start();
  for (let i = 0; i < 300; i++) sim.advance(10);
  assert.equal(sim.phase(), m.Phase.Play);
}

test('defaults() returns the C++ defaults', () => {
  const d = m.defaults();
  assert.deepEqual(d.rules, {
    n: 7, target: 4, diffOn: false, diff: 2,
    humanCooldownMs: 3000, machineCooldownMs: 1000, countdownMs: 3000, resumeCountdownMs: 1000,
    maxHeld: 1, humanIntervalMs: 1000, maxHoldMs: 1000, timeLimitMs: 600000,
    edgeCapture: false, wallLimit: false, untouchedMs: 0,
  });
  assert.equal(d.params.freeSpeed, 4);
  assert.equal(d.params.dragSpeed, 1.5);
  assert.equal(d.params.engageMs, 200);
  assert.equal(d.params.reactMs, 250);
  assert.equal(d.params.level, m.Level.Normal);
  assert.equal(d.params.aggression, 0.5);
  assert.equal(d.params.readLift, true);
  assert.equal(d.params.humanMoveMs, 500);
  assert.equal(d.params.humanLiftMs, 150);
  assert.equal(d.params.humanPlaceMs, 350);
  assert.equal(d.params.humanReactMs, 300);
  assert.deepEqual(d.bot, { reactMs: 400, moveMs: 500, moveJitter: 0.4, greed: 0.8, style: m.BotStyle.Random });
});

test('state() shape of a new 7x7 game', () => {
  withSim([], (sim) => {
    const s = sim.state();
    assert.equal(s.phase, m.Phase.Ready);
    assert.equal(s.winner, m.Winner.None);
    assert.equal(s.n, 7);
    assert.ok(Array.isArray(s.cells));
    assert.ok(Array.isArray(s.leds));
    assert.equal(s.cells.length, 49);
    assert.equal(s.leds.length, 49);
    assert.equal(s.pieces.length, 14);
    assert.equal(s.pieces.filter((p) => p.side === 1).length, 7);
    assert.equal(s.pieces.filter((p) => p.side === 2).length, 7);
    assert.equal(s.cells[0 * 7 + 3], 2); // machine row 0
    assert.equal(s.cells[6 * 7 + 3], 1); // human row 6
    assert.equal(s.stuckId, -1);
    assert.equal(s.job.active, false);
    assert.deepEqual(s.events, []);
    const p = s.pieces[0];
    for (const k of ['id', 'side', 'r', 'c', 'alive', 'held', 'cooldownLeft', 'x', 'y']) assert.ok(k in p, k);
    assert.equal(s.pendingId, -1);
  });
});

test('constructor and setRules take plain objects, missing fields use the defaults', () => {
  withSim([{ n: 5 }, { level: m.Level.Hard }, 7], (sim) => {
    const s = sim.state();
    assert.equal(s.n, 5);
    assert.equal(s.cells.length, 25);
    assert.equal(s.pieces.length, 10);
    sim.setRules({ n: 9 });
    sim.reset();
    assert.equal(sim.state().n, 9);
  });
  assert.throws(() => new m.SimDriver({ nn: 5 }, {}, 1), /nn/);
  assert.throws(() => new m.SimDriver({ maxHeld: 3 }, {}, 1), /maxHeld/);
});

test('lift and place return the C++ enums', () => {
  withSim([{}, {}, 1], (sim) => {
    assert.equal(sim.lift(6, 3), m.LiftResult.NotPlaying);
    startPlay(sim);
    assert.equal(sim.lift(0, 3), m.LiftResult.NotHumanPiece);
    assert.equal(sim.place(4, 3), m.PlaceResult.NoHeldPiece);
    assert.equal(sim.lift(6, 3), m.LiftResult.Ok);
    assert.equal(sim.state().pieces.find((p) => p.held)?.r, 6);
    assert.equal(sim.place(5, 4), m.PlaceResult.Illegal); // diagonal
    assert.equal(sim.place(6, 3), m.PlaceResult.Cancelled); // back to the origin
    assert.equal(sim.lift(6, 3), m.LiftResult.Ok);
    assert.equal(sim.place(4, 3), m.PlaceResult.Ok);
    assert.equal(sim.lift(4, 3), m.LiftResult.InCooldown);
    const p = sim.state().pieces.find((q) => q.r === 4 && q.c === 3);
    assert.equal(p.cooldownLeft, 3000);
  });
});

test('a placement within the interval is Pending until it lands; one piece in hand', () => {
  withSim([{}, {}, 1], (sim) => {
    startPlay(sim);
    assert.equal(sim.lift(6, 0), m.LiftResult.Ok);
    assert.equal(sim.lift(6, 1), m.LiftResult.TooMany);
    assert.equal(sim.place(5, 0), m.PlaceResult.Ok);
    assert.equal(sim.lift(6, 1), m.LiftResult.Ok);
    assert.equal(sim.place(4, 1), m.PlaceResult.Pending);
    let s = sim.state();
    assert.equal(s.pendingId, 4);
    assert.equal(s.pendingR, 4);
    assert.equal(s.pendingC, 1);
    assert.equal(s.cells[4 * 7 + 1], 0);
    for (let i = 0; i < 100; i++) sim.advance(10);
    s = sim.state();
    assert.equal(s.pendingId, -1);
    assert.equal(s.cells[4 * 7 + 1], 1);
  });
});

test('a held piece goes back after the hold limit', () => {
  withSim([{}, {}, 1], (sim) => {
    startPlay(sim);
    assert.equal(sim.lift(6, 2), m.LiftResult.Ok);
    for (let i = 0; i < 100; i++) sim.advance(10);
    const s = sim.state();
    assert.ok(!s.pieces.some((p) => p.held));
    assert.equal(s.cells[6 * 7 + 2], 1);
    assert.equal(s.pieces.find((p) => p.r === 6 && p.c === 2).cooldownLeft, 3000);
  });
});

test('a bot game ends Over at the latest at the 10 minute time limit', () => {
  const limit = m.defaults().rules.timeLimitMs;
  withSim([{}, {}, 3], (sim) => {
    sim.enableBot({});
    sim.start();
    let simMs = 0;
    while (sim.phase() !== m.Phase.Over && simMs < 3 * limit) {
      sim.advance(10);
      simMs += 10;
    }
    const s = sim.state();
    assert.equal(s.phase, m.Phase.Over, `phase ${s.phase.value} clock ${s.clockMs}`);
    assert.notEqual(s.winner, m.Winner.None);
    if (s.winner === m.Winner.Draw) {
      assert.equal(s.clockMs, limit);
      assert.equal(s.capturedHuman, s.capturedMachine);
    }
    assert.ok(s.machineMoves > 0);
  });
});

test('same seed, same game', () => {
  const run = (seed) =>
    withSim([{}, {}, seed], (sim) => {
      sim.enableBot({ greed: 1 });
      sim.start();
      for (let i = 0; i < 6000; i++) sim.advance(10);
      const s = sim.state();
      return [s.phase.value, s.clockMs, s.capturedHuman, s.capturedMachine, s.machineMoves, s.cells.join('')];
    });
  assert.deepEqual(run(5), run(5));
});

test('rule candidates: RW opens with one move each, then Play', () => {
  withSim([{ edgeCapture: true, wallLimit: true, untouchedMs: 20000 }, {}, 4], (sim) => {
    let s = sim.state();
    assert.equal(s.openingHuman, false);
    assert.equal(s.openingMachine, false);
    sim.start();
    for (let i = 0; i < 300 && sim.phase() !== m.Phase.Opening; i++) sim.advance(10);
    assert.equal(sim.phase(), m.Phase.Opening);
    assert.equal(sim.lift(6, 2), m.LiftResult.Ok);
    assert.equal(sim.place(4, 2), m.PlaceResult.Ok);
    assert.equal(sim.lift(6, 3), m.LiftResult.OpeningDone);
    s = sim.state();
    assert.equal(s.openingHuman, true);
    assert.equal(s.clockMs, 0);
    // The machine reads and makes its one move; then Play.
    for (let i = 0; i < 1000 && sim.phase() !== m.Phase.Play; i++) sim.advance(10);
    s = sim.state();
    assert.equal(s.phase, m.Phase.Play);
    assert.equal(s.openingMachine, true);
    assert.equal(s.machineMoves, 1);
    assert.equal(s.cells.filter((v, i) => v === 2 && i < 7).length, 6); // one piece left row 0
  });
  withSim([], (sim) => {
    const s = sim.state();
    assert.equal(s.openingHuman, true);
    assert.equal(s.openingMachine, true);
  });
  assert.throws(() => new m.SimDriver({ edge: true }, {}, 1), /edge/);
});

test('bot styles by name or enum, and the read counters in state()', () => {
  for (const style of ['random', 'trap', 'race', 'race3', m.BotStyle.Race]) {
    withSim([{}, {}, 2], (sim) => {
      sim.enableBot({ style });
      sim.start();
      for (let i = 0; i < 3000; i++) sim.advance(10);
      const s = sim.state();
      assert.ok(s.reads > 0, `${style}: reads`);
      assert.ok(s.safeReads >= 0 && s.safeReads <= s.reads);
      assert.ok(s.lostPredicted >= 0 && s.lostPredicted <= s.capturedHuman);
    });
  }
  withSim([], (sim) => {
    assert.throws(() => sim.enableBot({ style: 'minimax' }), /style/);
    assert.throws(() => sim.enableBot({ holdMs: 600 }), /holdMs/);
    assert.throws(() => sim.enableBot({ moveJitter: 2 }), /moveJitter/);
  });
});
