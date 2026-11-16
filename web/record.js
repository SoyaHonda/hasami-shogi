// Game records (plan 2026-11-06 3.12, plan 2026-11-12 7.4): what is needed to replay a game of the
// simulator exactly. The SimDriver is deterministic, so a record is the driver's construction
// (rules, params, seed), then every call made on it in order: each advance(dt) of the screen loop,
// each lift and place of the person, pause toggles and changes of the settings. Replaying the calls
// on a new driver gives the same game, tick for tick; the position at any moment (for instance when
// the person lifted a piece) comes from the replay. No personal data is recorded.
//
// No DOM here: web/app.js records with it, tools/replay.mjs replays with it under Node.js.
//
// A record (one JSON object per line in a .jsonl file):
//   format   1
//   id       random id of the record
//   date     ISO time the game was started (local clock of the browser)
//   build    {coreHash, commit} of the WASM build that played it (m.buildInfo()): a record replays
//            exactly only on a build of the same lib/core; check out `commit` to replay an old one
//   seed     the driver's seed
//   rules    the Rules the driver was made with
//   params   the Params it was made with (level as "Easy" | "Normal" | "Hard")
//   ops      the calls after start(), in order:
//              a string: advance() calls, one character per call, dt = DT_CHARS.indexOf(ch) + 1 ms
//              [dt]: one advance(dt) with dt above DT_CHARS.length
//              ["L", r, c, result]: lift(r, c) and its LiftResult name
//              ["P", r, c, result]: place(r, c) and its PlaceResult name
//              ["Z"]: togglePause()
//              ["S", params]: setParams(params)
//              ["R", rules]: setRules(rules)
//   end      "over" (the game ended), or "abandoned" (a new game or a reset came first)
//   result   at the end: phase, winner, capturedHuman, capturedMachine, clockMs, saves, machineMoves

export const FORMAT = 1;
export const DT_CHARS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';

// The name of an embind enum value (m.Level, m.LiftResult, ...), or the value itself.
export function enumName(enumObj, v) {
  for (const k of Object.keys(enumObj)) if (k !== 'values' && enumObj[k] === v) return k;
  return v;
}

// Params as plain JSON (the level by name; paramsFrom in wasm/bindings.cpp reads names).
export function plainParams(m, p) {
  return { ...p, level: enumName(m.Level, p.level) };
}

export function randomId() {
  const a = new Uint8Array(8);
  globalThis.crypto.getRandomValues(a);
  return Array.from(a, (b) => b.toString(16).padStart(2, '0')).join('');
}

export class Recorder {
  // m: the module (for enum names); rules, params: what the driver was made with; seed: its seed.
  constructor(m, { rules, params, seed, date = new Date().toISOString(), id = randomId() }) {
    this.m = m;
    const build = typeof m.buildInfo === 'function' ? m.buildInfo() : { coreHash: 'unknown', commit: 'unknown' };
    this.rec = { format: FORMAT, id, date, build, seed, rules: { ...rules }, params: plainParams(m, params), ops: [] };
    this.run = ''; // advance() calls not yet flushed into ops
  }

  flush() {
    if (this.run) {
      this.rec.ops.push(this.run);
      this.run = '';
    }
  }

  advance(dt) {
    if (dt >= 1 && dt <= DT_CHARS.length && Number.isInteger(dt)) {
      this.run += DT_CHARS[dt - 1];
    } else {
      this.flush();
      this.rec.ops.push([dt]);
    }
  }

  lift(r, c, result) {
    this.flush();
    this.rec.ops.push(['L', r, c, enumName(this.m.LiftResult, result)]);
  }

  place(r, c, result) {
    this.flush();
    this.rec.ops.push(['P', r, c, enumName(this.m.PlaceResult, result)]);
  }

  togglePause() {
    this.flush();
    this.rec.ops.push(['Z']);
  }

  setParams(params) {
    this.flush();
    this.rec.ops.push(['S', plainParams(this.m, params)]);
  }

  setRules(rules) {
    this.flush();
    this.rec.ops.push(['R', { ...rules }]);
  }

  // The finished record. state: the driver's state() at the end.
  finish(end, state) {
    this.flush();
    const m = this.m;
    this.rec.end = end;
    this.rec.result = {
      phase: enumName(m.Phase, state.phase),
      winner: enumName(m.Winner, state.winner),
      capturedHuman: state.capturedHuman,
      capturedMachine: state.capturedMachine,
      clockMs: state.clockMs,
      saves: state.saves,
      machineMoves: state.machineMoves,
    };
    return this.rec;
  }

  // Whether the person did anything (a record without a lift is not worth keeping).
  get touched() {
    return this.rec.ops.some((o) => Array.isArray(o) && o[0] === 'L');
  }
}

// Replays a record on a new driver and returns it (the caller deletes it). onOp(op, sim), when
// given, is called before each lift, place, pause and settings change, with the driver as it is
// at that moment (sim.state() is the position the person saw).
export function replay(m, rec, onOp) {
  if (rec.format !== FORMAT) throw new Error(`unknown record format ${rec.format}`);
  const sim = new m.SimDriver(rec.rules, rec.params, rec.seed >>> 0);
  sim.start();
  for (const op of rec.ops) {
    if (typeof op === 'string') {
      for (const ch of op) {
        const i = DT_CHARS.indexOf(ch);
        if (i < 0) throw new Error(`bad advance character ${ch}`);
        sim.advance(i + 1);
      }
      continue;
    }
    if (onOp) onOp(op, sim);
    switch (op[0]) {
      case 'L':
        sim.lift(op[1], op[2]);
        break;
      case 'P':
        sim.place(op[1], op[2]);
        break;
      case 'Z':
        sim.togglePause();
        break;
      case 'S':
        sim.setParams(op[1]);
        break;
      case 'R':
        sim.setRules(op[1]);
        break;
      default:
        if (op.length === 1 && typeof op[0] === 'number') sim.advance(op[0]);
        else throw new Error(`bad op ${JSON.stringify(op)}`);
    }
  }
  return sim;
}

// The record's result as replay() sees it, for checking a record against its own result.
export function resultOf(m, sim) {
  const s = sim.state();
  return {
    phase: enumName(m.Phase, s.phase),
    winner: enumName(m.Winner, s.winner),
    capturedHuman: s.capturedHuman,
    capturedMachine: s.capturedMachine,
    clockMs: s.clockMs,
    saves: s.saves,
    machineMoves: s.machineMoves,
  };
}

// Saved records in the browser (IndexedDB; a list in memory when it is not available).
export class RecordStore {
  constructor(name = 'hasami-records') {
    this.name = name;
    this.memory = [];
    this.db = null;
    this.ready = this.open();
  }

  async open() {
    try {
      if (!globalThis.indexedDB) return;
      this.db = await new Promise((resolve, reject) => {
        const req = indexedDB.open(this.name, 1);
        req.onupgradeneeded = () => req.result.createObjectStore('games', { keyPath: 'id' });
        req.onsuccess = () => resolve(req.result);
        req.onerror = () => reject(req.error);
      });
    } catch {
      this.db = null;
    }
  }

  async tx(mode, f) {
    await this.ready;
    if (!this.db) return f(null);
    return new Promise((resolve, reject) => {
      const t = this.db.transaction('games', mode);
      const store = t.objectStore('games');
      let out;
      Promise.resolve(f(store)).then((v) => (out = v));
      t.oncomplete = () => resolve(out);
      t.onerror = () => reject(t.error);
      t.onabort = () => reject(t.error);
    });
  }

  async add(rec) {
    try {
      await this.tx('readwrite', (s) => (s ? s.put(rec) : this.memory.push(rec)));
    } catch {
      this.memory.push(rec);
    }
  }

  async all() {
    try {
      const fromDb = await this.tx('readonly', (s) =>
        s
          ? new Promise((resolve, reject) => {
              const req = s.getAll();
              req.onsuccess = () => resolve(req.result);
              req.onerror = () => reject(req.error);
            })
          : [],
      );
      return [...(fromDb || []), ...this.memory].sort((a, b) => (a.date < b.date ? -1 : a.date > b.date ? 1 : 0));
    } catch {
      return [...this.memory];
    }
  }

  async clear() {
    this.memory = [];
    try {
      await this.tx('readwrite', (s) => s && s.clear());
    } catch {
      /* nothing more to clear */
    }
  }
}
