// Automatic games on the WASM build (plan 5.8): HumanBot against the machine, advance(10) in
// fixed steps, one table row per condition (level x human cooldown).
//
//   node tools/autoplay.mjs --games 500 --level easy,normal,hard --hcd 2,3,4 --seed 1 [--csv]
//
// Game i of a condition uses seed (--seed + i). A game whose clock passes --max-min minutes is
// stopped and counted as a draw. Rules and machine parameters not given here are the C++
// defaults (defaults() of the module).
import { parseArgs } from 'node:util';
import { loadHasami } from './load-wasm.mjs';

const STEP_MS = 10;

function usage(msg) {
  if (msg) console.error(`autoplay: ${msg}`);
  console.error(
    'usage: node tools/autoplay.mjs [--games N] [--level easy,normal,hard] [--hcd SECONDS,...]\n' +
      '                              [--seed N] [--max-min MINUTES] [--csv]',
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
      hcd: { type: 'string' },
      seed: { type: 'string', default: '1' },
      'max-min': { type: 'string', default: '10' },
      csv: { type: 'boolean', default: false },
      help: { type: 'boolean', short: 'h', default: false },
    },
  }).values;
} catch (e) {
  usage(e.message);
}
if (opts.help) usage();

const m = await loadHasami();
const defaults = m.defaults();
const levelName = (lv) => Object.keys(m.Level).find((k) => m.Level[k] === lv) ?? String(lv?.value);

const games = positiveNumber('games', opts.games, { integer: true });
const seed0 = positiveNumber('seed', opts.seed, { integer: true, allowZero: true });
const maxClockMs = positiveNumber('max-min', opts['max-min']) * 60_000;
const levels = opts.level
  ? list(opts.level).map((s) => {
      const key = Object.keys(m.Level).find((k) => k.toLowerCase() === s.toLowerCase());
      if (!key || typeof m.Level[key] !== 'object') usage(`unknown level: ${s}`);
      return m.Level[key];
    })
  : [defaults.params.level];
const cooldownsMs = opts.hcd
  ? list(opts.hcd).map((s) => Math.round(positiveNumber('hcd', s, { allowZero: true }) * 1000))
  : [defaults.rules.humanCooldownMs];
if (levels.length === 0 || cooldownsMs.length === 0) usage('empty --level or --hcd');

// One game; a new driver each time so that the bot's random sequence starts from `seed`.
function playOne(level, humanCooldownMs, seed) {
  const sim = new m.SimDriver({ humanCooldownMs }, { level }, seed >>> 0);
  try {
    sim.enableBot({});
    sim.start();
    // Safety net on simulated time: the clock stands still outside Play.
    const maxSimMs = 3 * maxClockMs + 60_000;
    let simMs = 0;
    while (sim.phase() !== m.Phase.Over && sim.clockMs() <= maxClockMs && simMs < maxSimMs) {
      sim.advance(STEP_MS);
      simMs += STEP_MS;
    }
    const s = sim.state();
    const over = s.phase === m.Phase.Over && s.winner !== m.Winner.None;
    return { winner: over ? s.winner : m.Winner.None, clockMs: s.clockMs, saves: s.saves };
  } finally {
    sim.delete();
  }
}

const rows = [];
for (const level of levels) {
  for (const hcd of cooldownsMs) {
    let draws = 0, machine = 0, human = 0, clockSum = 0, savesSum = 0;
    for (let i = 0; i < games; i++) {
      const r = playOne(level, hcd, seed0 + i);
      if (r.winner === m.Winner.Machine) machine++;
      else if (r.winner === m.Winner.Human) human++;
      else draws++;
      clockSum += r.clockMs;
      savesSum += r.saves;
    }
    const decided = machine + human;
    rows.push({
      level: levelName(level).toLowerCase(),
      hcd: hcd / 1000,
      games,
      draws,
      machineWins: machine,
      machineWinRate: decided > 0 ? machine / decided : null,
      avgMin: clockSum / games / 60_000,
      avgSaves: savesSum / games,
    });
  }
}

const header = ['level', 'hcd_s', 'games', 'draws', 'machine_wins', 'machine_win_rate', 'avg_min', 'avg_saves'];
const cells = rows.map((r) => [
  r.level,
  String(r.hcd),
  String(r.games),
  String(r.draws),
  String(r.machineWins),
  r.machineWinRate === null ? (opts.csv ? '' : '-') : opts.csv ? r.machineWinRate.toFixed(4) : `${(r.machineWinRate * 100).toFixed(1)}%`,
  r.avgMin.toFixed(2),
  r.avgSaves.toFixed(2),
]);

if (opts.csv) {
  console.log(header.join(','));
  for (const c of cells) console.log(c.join(','));
} else {
  const widths = header.map((h, i) => Math.max(h.length, ...cells.map((c) => c[i].length)));
  const line = (c) => c.map((x, i) => (i === 0 ? x.padEnd(widths[i]) : x.padStart(widths[i]))).join('  ');
  console.log(line(header));
  console.log(widths.map((w) => '-'.repeat(w)).join('  '));
  for (const c of cells) console.log(line(c));
  console.log(
    `\nseed ${seed0}..${seed0 + games - 1} per condition, draw = no winner within ${maxClockMs / 60_000} min of game clock;` +
      ' machine_win_rate excludes draws; avg_min = mean game clock; avg_saves = mean pre-emption successes',
  );
}
