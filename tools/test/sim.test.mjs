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
  });
  assert.equal(d.params.freeSpeed, 4);
  assert.equal(d.params.dragSpeed, 1.5);
  assert.equal(d.params.engageMs, 200);
  assert.equal(d.params.reactMs, 250);
  assert.equal(d.params.level, m.Level.Normal);
  assert.equal(d.params.aggression, 0.5);
  assert.equal(d.params.readLift, true);
  assert.deepEqual(d.bot, { reactMs: 400, holdMs: 600, greed: 0.8 });
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

test('a bot game ends Over or at the 10 minute clock limit', () => {
  const maxClock = 10 * 60 * 1000;
  withSim([{}, {}, 3], (sim) => {
    sim.enableBot({});
    sim.start();
    let simMs = 0;
    while (sim.phase() !== m.Phase.Over && sim.clockMs() < maxClock && simMs < 3 * maxClock) {
      sim.advance(10);
      simMs += 10;
    }
    const s = sim.state();
    assert.ok(s.phase === m.Phase.Over || s.clockMs >= maxClock, `phase ${s.phase.value} clock ${s.clockMs}`);
    if (s.phase === m.Phase.Over) assert.notEqual(s.winner, m.Winner.None);
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
