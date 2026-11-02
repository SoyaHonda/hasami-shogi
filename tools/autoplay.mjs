// Automatic games on the WASM build (plan 5.8, plan 2026-10-31 3.12-3.13): a human bot against
// the machine, advance(10) in fixed steps, one table row per condition (level x bot x human
// cooldown).
//
//   node tools/autoplay.mjs --games 500 --level normal,hard --bot random,trap,race,race3 \
//        [--hcd 3] [--seed 1] [--max-min 10] [--csv]
//        [--baseline-out FILE] [--baseline FILE ...] [--note TEXT]
//
// Game i of a condition uses seed (--seed + i). Every game ends in the Game itself: by captures,
// or at the time limit (--max-min minutes of game clock, Rules::timeLimitMs), where the side with
// more captures wins and equal captures is a draw. Rules and machine parameters not given here
// are the C++ defaults (defaults() of the module).
//
// --baseline-out writes every game's measures per condition to FILE (JSON). --baseline reads such
// files (several may be given) and compares each condition of this run with the same condition
// there, game by game on the same seed: the paired mean difference (this run - baseline) with a
// paired bootstrap confidence interval (10000 resamples; 98.3 % = 1 - 0.05/3 for machine_lost,
// Bonferroni over the three opponents, 95 % for capture_diff) and, for reference, the two-sided
// Wilcoxon signed-rank p value (normal approximation with tie correction; zero differences are
// dropped).
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname } from 'node:path';
import { parseArgs } from 'node:util';
import { loadHasami } from './load-wasm.mjs';

const STEP_MS = 10;
const FORMAT = 'hasami-autoplay/1';
const BOOTSTRAP = 10_000;
const BOTS = ['random', 'trap', 'race', 'race3'];
// Compared measures and the confidence level of each interval (plan 2026-10-31 3.13).
const COMPARED = [
  { key: 'lost', name: 'machine_lost', level: 1 - 0.05 / 3 },
  { key: 'diff', name: 'capture_diff', level: 0.95 },
];

function usage(msg) {
  if (msg) console.error(`autoplay: ${msg}`);
  console.error(
    'usage: node tools/autoplay.mjs [--games N] [--level easy,normal,hard] [--bot random,trap,race,race3]\n' +
      '                              [--hcd SECONDS,...] [--seed N] [--max-min MINUTES] [--csv]\n' +
      '                              [--baseline-out FILE] [--baseline FILE ...] [--note TEXT]',
  );
  process.exit(2);
}

function positiveNumber(name, s, { integer = false, allowZero = false } = {}) {
  const v = Number(s);
  const ok = Number.isFinite(v) && (allowZero ? v >= 0 : v > 0) && (!integer || Number.isInteger(v));
  if (!ok) usage(`--${name} must be a ${integer ? 'whole ' : ''}number ${allowZero ? '>= 0' : '> 0'}: ${s}`);
  return v;
}

function list(s) {
  return s.split(',').map((x) => x.trim()).filter((x) => x !== '');
}

let opts;
try {
  opts = parseArgs({
    options: {
      games: { type: 'string', default: '100' },
      level: { type: 'string' },
      bot: { type: 'string', default: 'random' },
      hcd: { type: 'string' },
      seed: { type: 'string', default: '1' },
      'max-min': { type: 'string', default: '10' },
      csv: { type: 'boolean', default: false },
      'baseline-out': { type: 'string' },
      baseline: { type: 'string', multiple: true },
      note: { type: 'string' },
      help: { type: 'boolean', short: 'h', default: false },
    },
  }).values;
} catch (e) {
  usage(e.message);
}
if (opts.help) usage();

const m = await loadHasami();
const defaults = m.defaults();
const levelName = (lv) => (Object.keys(m.Level).find((k) => m.Level[k] === lv) ?? String(lv?.value)).toLowerCase();

const games = positiveNumber('games', opts.games, { integer: true });
const seed0 = positiveNumber('seed', opts.seed, { integer: true, allowZero: true });
const maxMin = positiveNumber('max-min', opts['max-min']);
const maxClockMs = Math.round(maxMin * 60_000);
if (maxClockMs > 0xffffffff) usage('--max-min is too large');
const levels = opts.level
  ? list(opts.level).map((s) => {
      const key = Object.keys(m.Level).find((k) => k.toLowerCase() === s.toLowerCase());
      if (!key || typeof m.Level[key] !== 'object') usage(`unknown level: ${s}`);
      return m.Level[key];
    })
  : [defaults.params.level];
const bots = list(opts.bot).map((s) => {
  const b = s.toLowerCase();
  if (!BOTS.includes(b)) usage(`unknown bot: ${s} (one of ${BOTS.join(', ')})`);
  return b;
});
const cooldownsMs = opts.hcd
  ? list(opts.hcd).map((s) => Math.round(positiveNumber('hcd', s, { allowZero: true }) * 1000))
  : [defaults.rules.humanCooldownMs];
if (levels.length === 0 || bots.length === 0 || cooldownsMs.length === 0) usage('empty --level, --bot or --hcd');

const conditionKey = (level, bot, hcdMs) => `${level}/${bot}/hcd${hcdMs / 1000}`;

// One game; a new driver each time so that the bot's random sequence starts from `seed`.
function playOne(level, bot, humanCooldownMs, seed) {
  const sim = new m.SimDriver({ humanCooldownMs, timeLimitMs: maxClockMs }, { level }, seed >>> 0);
  try {
    sim.enableBot({ style: bot });
    sim.start();
    // Safety net on simulated time: the clock stands still outside Play.
    const maxSimMs = 3 * maxClockMs + 60_000;
    let simMs = 0;
    while (sim.phase() !== m.Phase.Over && simMs < maxSimMs) {
      sim.advance(STEP_MS);
      simMs += STEP_MS;
    }
    const s = sim.state();
    const over = s.phase === m.Phase.Over;
    const winner = over ? s.winner : m.Winner.None;
    if (!over) console.error(`autoplay: seed ${seed} did not finish (stuck outside Play)`);
    return {
      seed,
      lost: s.capturedHuman, // machine pieces the human captured
      taken: s.capturedMachine, // human pieces the machine captured
      diff: s.capturedMachine - s.capturedHuman,
      win: winner === m.Winner.Machine ? 1 : 0,
      draw: winner === m.Winner.Draw ? 1 : 0,
      finished: over ? 1 : 0,
      clockMs: s.clockMs,
      reads: s.reads,
      safeReads: s.safeReads,
      lostPredicted: s.lostPredicted,
      saves: s.saves,
    };
  } finally {
    sim.delete();
  }
}

const sum = (xs, f) => xs.reduce((a, x) => a + f(x), 0);
const mean = (xs) => (xs.length ? xs.reduce((a, x) => a + x, 0) / xs.length : NaN);

function summarize(level, bot, hcd, results) {
  const n = results.length;
  const reads = sum(results, (r) => r.reads);
  const lost = sum(results, (r) => r.lost);
  return {
    level: levelName(level),
    bot,
    hcd: hcd / 1000,
    games: n,
    lostPerGame: lost / n,
    diffPerGame: sum(results, (r) => r.diff) / n,
    winRate: sum(results, (r) => r.win) / n, // a draw counts as not a win
    drawRate: sum(results, (r) => r.draw) / n,
    safeReadRate: reads > 0 ? sum(results, (r) => r.safeReads) / reads : null,
    predictedRate: lost > 0 ? sum(results, (r) => r.lostPredicted) / lost : null,
    avgMin: sum(results, (r) => r.clockMs) / n / 60_000,
    unfinished: n - sum(results, (r) => r.finished),
  };
}

// ---- Statistics for the paired comparison.

// Seeded uniform numbers in [0, 1) (mulberry32), so that the bootstrap is reproducible.
function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function hashString(s) {
  let h = 0x811c9dc5;
  for (let i = 0; i < s.length; i++) h = Math.imul(h ^ s.charCodeAt(i), 0x01000193) >>> 0;
  return h;
}

// Percentile bootstrap interval of the mean of d at `level`.
function bootstrapCi(d, level, seed) {
  const rnd = mulberry32(seed);
  const n = d.length;
  const means = new Float64Array(BOOTSTRAP);
  for (let b = 0; b < BOOTSTRAP; b++) {
    let s = 0;
    for (let i = 0; i < n; i++) s += d[Math.floor(rnd() * n)];
    means[b] = s / n;
  }
  means.sort();
  const lo = (1 - level) / 2;
  const at = (q) => means[Math.min(BOOTSTRAP - 1, Math.max(0, Math.floor(q * BOOTSTRAP)))];
  return [at(lo), at(1 - lo)];
}

// Standard normal CDF (Abramowitz and Stegun 7.1.26 for erf; error below 1.5e-7).
function normCdf(z) {
  const x = Math.abs(z) / Math.SQRT2;
  const t = 1 / (1 + 0.3275911 * x);
  const y = 1 - ((((1.061405429 * t - 1.453152027) * t + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * Math.exp(-x * x);
  return z >= 0 ? 0.5 * (1 + y) : 0.5 * (1 - y);
}

// Two-sided Wilcoxon signed-rank p value of d (zeros dropped, average ranks for ties, normal
// approximation with tie and continuity correction). null when every difference is zero.
function wilcoxonP(d) {
  const nz = d.filter((x) => x !== 0).map((x) => ({ a: Math.abs(x), s: Math.sign(x) }));
  const n = nz.length;
  if (n === 0) return null;
  nz.sort((p, q) => p.a - q.a);
  let wPlus = 0;
  let tieTerm = 0;
  for (let i = 0; i < n; ) {
    let j = i;
    while (j < n && nz[j].a === nz[i].a) j++;
    const rank = (i + 1 + j) / 2; // average of ranks i+1..j
    for (let k = i; k < j; k++) if (nz[k].s > 0) wPlus += rank;
    const t = j - i;
    tieTerm += t * t * t - t;
    i = j;
  }
  const mu = (n * (n + 1)) / 4;
  const variance = (n * (n + 1) * (2 * n + 1)) / 24 - tieTerm / 48;
  if (variance <= 0) return null;
  const diff = wPlus - mu;
  const z = (Math.abs(diff) - 0.5 > 0 ? Math.abs(diff) - 0.5 : 0) / Math.sqrt(variance);
  return Math.min(1, 2 * (1 - normCdf(z)));
}

function loadBaselines(files) {
  const merged = {};
  for (const f of files) {
    let data;
    try {
      data = JSON.parse(readFileSync(f, 'utf8'));
    } catch (e) {
      usage(`cannot read baseline ${f}: ${e.message}`);
    }
    if (data?.format !== FORMAT || typeof data.conditions !== 'object') usage(`${f} is not a ${FORMAT} file`);
    for (const [k, v] of Object.entries(data.conditions)) merged[k] = v;
  }
  return merged;
}

// ---- Run.

const baseline = opts.baseline ? loadBaselines(opts.baseline) : null;
const rows = [];
const perCondition = {};
for (const level of levels) {
  for (const bot of bots) {
    for (const hcd of cooldownsMs) {
      const results = [];
      for (let i = 0; i < games; i++) results.push(playOne(level, bot, hcd, seed0 + i));
      rows.push(summarize(level, bot, hcd, results));
      perCondition[conditionKey(levelName(level), bot, hcd)] = { level: levelName(level), bot, hcd: hcd / 1000, games: results };
    }
  }
}

const pct = (v) => (v === null ? (opts.csv ? '' : '-') : opts.csv ? v.toFixed(4) : `${(v * 100).toFixed(1)}%`);
const fix = (v, d = 3) => (v === null || Number.isNaN(v) ? (opts.csv ? '' : '-') : v.toFixed(d));

function printTable(header, cells) {
  if (opts.csv) {
    console.log(header.join(','));
    for (const c of cells) console.log(c.join(','));
    return;
  }
  const widths = header.map((h, i) => Math.max(h.length, ...cells.map((c) => c[i].length)));
  const line = (c) => c.map((x, i) => (i <= 1 ? x.padEnd(widths[i]) : x.padStart(widths[i]))).join('  ');
  console.log(line(header));
  console.log(widths.map((w) => '-'.repeat(w)).join('  '));
  for (const c of cells) console.log(line(c));
}

printTable(
  ['level', 'bot', 'hcd_s', 'games', 'machine_lost', 'capture_diff', 'machine_win', 'draw', 'safe_reads', 'predicted', 'avg_min'],
  rows.map((r) => [
    r.level,
    r.bot,
    String(r.hcd),
    String(r.games),
    fix(r.lostPerGame),
    fix(r.diffPerGame),
    pct(r.winRate),
    pct(r.drawRate),
    pct(r.safeReadRate),
    pct(r.predictedRate),
    fix(r.avgMin, 2),
  ]),
);
if (!opts.csv) {
  console.log(
    `\nseed ${seed0}..${seed0 + games - 1} per condition, ${maxMin} min time limit (more captures wins there, equal is a draw).` +
      '\nmachine_lost = machine pieces captured per game; capture_diff = (machine captures - human captures) per game;' +
      '\nmachine_win counts a draw as not a win; safe_reads = reads with at least one candidate counted safe;' +
      '\npredicted = machine pieces lost with risk >= 0.5 in the latest read at least the level\'s margin before the capture.',
  );
  const unfinished = rows.reduce((a, r) => a + r.unfinished, 0);
  if (unfinished > 0) console.log(`${unfinished} game(s) did not finish.`);
}

if (baseline) {
  const cells = [];
  let missing = 0;
  for (const [key, cur] of Object.entries(perCondition)) {
    const base = baseline[key];
    if (!base) {
      missing++;
      continue;
    }
    const before = new Map(base.games.map((g) => [g.seed, g]));
    const pairs = cur.games.filter((g) => before.has(g.seed)).map((g) => [before.get(g.seed), g]);
    if (pairs.length === 0) {
      missing++;
      continue;
    }
    for (const c of COMPARED) {
      const d = pairs.map(([b, a]) => a[c.key] - b[c.key]);
      const [lo, hi] = bootstrapCi(d, c.level, hashString(`${key}/${c.key}`));
      const p = wilcoxonP(d);
      cells.push([
        `${cur.level}/${cur.bot}`,
        c.name,
        String(cur.hcd),
        String(pairs.length),
        fix(mean(pairs.map(([b]) => b[c.key]))),
        fix(mean(pairs.map(([, a]) => a[c.key]))),
        fix(mean(d)),
        `${(c.level * 100).toFixed(1)}%`,
        fix(lo),
        fix(hi),
        p === null ? (opts.csv ? '' : '-') : p.toPrecision(3),
      ]);
    }
  }
  console.log(opts.csv ? '' : '\npaired comparison with the baseline (difference = this run - baseline, same seed):');
  printTable(['condition', 'measure', 'hcd_s', 'pairs', 'baseline', 'now', 'mean_diff', 'ci', 'ci_lo', 'ci_hi', 'wilcoxon_p'], cells);
  if (missing > 0) console.error(`autoplay: ${missing} condition(s) of this run are not in the baseline`);
}

if (opts['baseline-out']) {
  const out = {
    format: FORMAT,
    created: new Date().toISOString(),
    note: opts.note ?? '',
    seeds: [seed0, seed0 + games - 1],
    maxMin,
    rules: { ...defaults.rules, timeLimitMs: maxClockMs },
    params: { ...defaults.params, level: undefined },
    bot: { ...defaults.bot, style: undefined },
    conditions: perCondition,
  };
  const file = opts['baseline-out'];
  mkdirSync(dirname(file), { recursive: true });
  // One line per game.
  const text = JSON.stringify(out, null, 1).replace(/\{\n\s*"seed"[^{}]*\}/g, (g) => g.replace(/\s*\n\s*/g, '').replace(/": /g, '":'));
  writeFileSync(file, `${text}\n`);
  if (!opts.csv) console.log(`\nwrote ${file}`);
}
