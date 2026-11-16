// Replays game records of the simulator (web/record.js) with the WASM build of lib/core.
//
//   node tools/replay.mjs data/games/*.jsonl                       # check every record
//   node tools/replay.mjs data/games/*.jsonl --moves moves.jsonl   # and write the person's moves
//
// Each record is replayed on a new driver and its end compared with the record's own result
// (a mismatch means the record does not replay: another build of lib/core, or a lost call). A
// record made by another build of lib/core (its build.coreHash) is reported with the commit to
// check out to replay it. With
// --moves, every lift of the person that went through is written as one JSON line: the record's id,
// the game clock, the position the person saw just before lifting (state() of the driver: board,
// pieces with cooldowns, head, the machine's job, captures), the square lifted and the square the
// piece was put on (its origin for a cancel; null when it never came down, e.g. the hold limit).
// Needs web/hasami.js (npm run build:wasm).
import { readFileSync, writeFileSync } from 'node:fs';
import { loadHasami } from './load-wasm.mjs';
import { enumName, replay, resultOf } from '../web/record.js';

const args = process.argv.slice(2);
const movesAt = args.indexOf('--moves');
const movesPath = movesAt >= 0 ? args[movesAt + 1] : null;
const files = args.filter((a, i) => movesAt < 0 || (i !== movesAt && i !== movesAt + 1));
if (!files.length) {
  console.error('usage: node tools/replay.mjs FILE.jsonl... [--moves OUT.jsonl]');
  process.exit(2);
}

// The position as plain JSON (embind enums by name).
function positionOf(m, s) {
  return {
    phase: enumName(m.Phase, s.phase),
    clockMs: s.clockMs,
    n: s.n,
    cells: Array.from(s.cells).slice(0, s.n * s.n),
    pieces: s.pieces.map((p) => ({ id: p.id, side: p.side, r: p.r, c: p.c, alive: p.alive, held: p.held, cooldownLeft: p.cooldownLeft })),
    head: [s.headR, s.headC],
    job: { active: s.job.active, pieceId: s.job.pieceId, to: [s.job.toR, s.job.toC], removing: s.job.removing },
    capturedHuman: s.capturedHuman,
    capturedMachine: s.capturedMachine,
    pending: s.pendingId >= 0 ? { id: s.pendingId, r: s.pendingR, c: s.pendingC } : null,
  };
}

const m = await loadHasami();
const here = m.buildInfo();
const moves = [];
let games = 0, bad = 0, over = 0, humanWins = 0, machineWins = 0;
for (const file of files) {
  const lines = readFileSync(file, 'utf8').split('\n').filter((l) => l.trim());
  for (const line of lines) {
    const rec = JSON.parse(line);
    games++;
    let open = null;
    const sim = replay(m, rec, (op, s) => {
      if (!movesPath) return;
      if (op[0] === 'L' && op[3] === 'Ok') {
        if (open) moves.push(open);
        open = { id: rec.id, clockMs: s.clockMs(), position: positionOf(m, s.state()), lift: [op[1], op[2]], place: null };
      } else if (op[0] === 'P' && open && ['Ok', 'Pending', 'Cancelled'].includes(op[3])) {
        open.place = [op[1], op[2]];
        moves.push(open);
        open = null;
      }
    });
    if (open) moves.push(open);
    const got = resultOf(m, sim);
    sim.delete();
    const same = JSON.stringify(got) === JSON.stringify(rec.result);
    if (!same) {
      bad++;
      console.log(`${rec.id} (${file}): does not replay\n  record ${JSON.stringify(rec.result)}\n  replay ${JSON.stringify(got)}`);
      if (rec.build && rec.build.coreHash !== here.coreHash)
        console.log(`  made by another lib/core (${rec.build.coreHash}, this build ${here.coreHash}): replay it at commit ${rec.build.commit}`);
    }
    if (rec.end === 'over') over++;
    if (got.winner === 'Human') humanWins++;
    if (got.winner === 'Machine') machineWins++;
  }
}
console.log(`${games} records (${over} finished): ${games - bad} replay, ${bad} do not; you won ${humanWins}, the machine ${machineWins}`);
if (movesPath) {
  writeFileSync(movesPath, moves.map((x) => JSON.stringify(x)).join('\n') + (moves.length ? '\n' : ''));
  console.log(`${moves.length} moves -> ${movesPath}`);
}
process.exit(bad ? 1 : 0);
