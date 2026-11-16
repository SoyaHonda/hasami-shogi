// Game records (plan 2026-11-06 3.12, plan 2026-11-12 7.4): what is needed to replay a game of the
// simulator exactly. The SimDriver is deterministic, so a record is the driver's construction
// (rules, params, seed), then every call made on it in order: each advance(dt) of the screen loop,
// each lift and place of the person, pause toggles and changes of the settings. Replaying the calls
// on a new driver gives the same game, tick for tick; the position at any moment (for instance when
// the person lifted a piece) comes from the replay. No personal data is recorded.
//
// No DOM here: web/app.js records with it, tools/replay.mjs replays with it under Node.js. The
// page can also send each record to a GitHub repository with the person's own token
// (uploadRecord), one file per game.
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

// Saved records in the browser (IndexedDB; a list in memory when it is not available), and the
// ids of those already sent to GitHub.
export class RecordStore {
  constructor(name = 'hasami-records') {
    this.name = name;
    this.memory = [];
    this.memorySent = new Set();
    this.db = null;
    this.ready = this.open();
  }

  async open() {
    try {
      if (!globalThis.indexedDB) return;
      this.db = await new Promise((resolve, reject) => {
        const req = indexedDB.open(this.name, 2);
        req.onupgradeneeded = () => {
          const db = req.result;
          if (!db.objectStoreNames.contains('games')) db.createObjectStore('games', { keyPath: 'id' });
          if (!db.objectStoreNames.contains('sent')) db.createObjectStore('sent', { keyPath: 'id' });
        };
        req.onsuccess = () => resolve(req.result);
        req.onerror = () => reject(req.error);
      });
    } catch {
      this.db = null;
    }
  }

  // f(objectStore) inside a transaction on `name`; f(null) without IndexedDB.
  async tx(name, mode, f) {
    await this.ready;
    if (!this.db) return f(null);
    return new Promise((resolve, reject) => {
      const t = this.db.transaction(name, mode);
      const store = t.objectStore(name);
      let out;
      Promise.resolve(f(store)).then((v) => (out = v));
      t.oncomplete = () => resolve(out);
      t.onerror = () => reject(t.error);
      t.onabort = () => reject(t.error);
    });
  }

  static getAll(s) {
    return new Promise((resolve, reject) => {
      const req = s.getAll();
      req.onsuccess = () => resolve(req.result);
      req.onerror = () => reject(req.error);
    });
  }

  async add(rec) {
    try {
      await this.tx('games', 'readwrite', (s) => (s ? s.put(rec) : this.memory.push(rec)));
    } catch {
      this.memory.push(rec);
    }
  }

  async all() {
    try {
      const fromDb = await this.tx('games', 'readonly', (s) => (s ? RecordStore.getAll(s) : []));
      return [...(fromDb || []), ...this.memory].sort((a, b) => (a.date < b.date ? -1 : a.date > b.date ? 1 : 0));
    } catch {
      return [...this.memory];
    }
  }

  async markSent(id) {
    this.memorySent.add(id);
    try {
      await this.tx('sent', 'readwrite', (s) => s && s.put({ id }));
    } catch {
      /* kept in memory */
    }
  }

  async sentIds() {
    try {
      const rows = await this.tx('sent', 'readonly', (s) => (s ? RecordStore.getAll(s) : []));
      return new Set([...(rows || []).map((r) => r.id), ...this.memorySent]);
    } catch {
      return new Set(this.memorySent);
    }
  }

  // Forgets the records (the sent marks stay, so that a record is never sent twice).
  async clear() {
    this.memory = [];
    try {
      await this.tx('games', 'readwrite', (s) => s && s.clear());
    } catch {
      /* nothing more to clear */
    }
  }
}

// ---- Sending records to a GitHub repository (the person's own token, kept in their browser).

// The file of a record in the repository: <dir>/<day>/<date>-<id>.json (one line, as in .jsonl).
export function recordPath(rec, dir = 'games') {
  const d = dir.replace(/^\/+|\/+$/g, '');
  const day = String(rec.date).slice(0, 10);
  const stamp = String(rec.date).replace(/[:.]/g, '-');
  return `${d ? d + '/' : ''}${day}/${stamp}-${rec.id}.json`;
}

function base64Utf8(text) {
  const bytes = new TextEncoder().encode(text);
  let bin = '';
  for (let i = 0; i < bytes.length; i += 0x8000) bin += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(bin);
}

// Creates the record's file through the GitHub contents API. cfg: { repo: "owner/name", token,
// branch, dir }. The commit message carries [skip ci] (no workflow runs for it). A file that is
// already there counts as sent (the same record sent before). Returns { ok, status, message }.
export async function uploadRecord(rec, cfg, fetchImpl = globalThis.fetch) {
  const repo = String(cfg.repo || '').trim();
  if (!/^[\w.-]+\/[\w.-]+$/.test(repo)) return { ok: false, status: 0, message: 'リポジトリは owner/name の形で書いてください' };
  if (!cfg.token) return { ok: false, status: 0, message: 'トークンがありません' };
  const path = recordPath(rec, cfg.dir ?? 'games');
  const body = { message: `game ${rec.id} [skip ci]`, content: base64Utf8(JSON.stringify(rec) + '\n') };
  if (cfg.branch) body.branch = cfg.branch;
  let res;
  try {
    res = await fetchImpl(`https://api.github.com/repos/${repo}/contents/${path.split('/').map(encodeURIComponent).join('/')}`, {
      method: 'PUT',
      headers: {
        Accept: 'application/vnd.github+json',
        Authorization: `Bearer ${cfg.token}`,
        'X-GitHub-Api-Version': '2022-11-28',
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(body),
    });
  } catch (e) {
    return { ok: false, status: 0, message: `送れませんでした（${e && e.message ? e.message : e}）` };
  }
  if (res.status === 200 || res.status === 201) return { ok: true, status: res.status, message: path };
  let detail = '';
  try {
    detail = (await res.json()).message || '';
  } catch {
    /* no JSON body */
  }
  if (res.status === 422 && /sha/i.test(detail)) return { ok: true, status: 422, message: `${path}（送信済み）` };
  const hint =
    res.status === 401 ? 'トークンが正しくないか、期限が切れています'
    : res.status === 403 || res.status === 404 ? 'リポジトリの名前か、トークンの権限（Contents の読み書き）を確かめてください'
    : `GitHub が ${res.status} を返しました`;
  return { ok: false, status: res.status, message: `${hint}${detail ? `（${detail}）` : ''}` };
}
