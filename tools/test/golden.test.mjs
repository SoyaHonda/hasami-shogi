// The golden AI table that test_ai (PC) and the ESP32 `golden` command also read: WASM must
// pick the same moves.
import { test, before } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { loadHasami } from '../load-wasm.mjs';

const table = new URL('../../test/golden/ai_cases.txt', import.meta.url);

let m;
before(async () => {
  m = await loadHasami();
});

test('thinkCase rejects malformed lines', () => {
  assert.equal(m.thinkCase('M...H.... - 0,0 - 3,1.5,100,250,H,0.25,0').parsed, false);
  assert.equal(m.thinkCase('# comment').parsed, false);
});

test('WASM matches every line of test/golden/ai_cases.txt', () => {
  const lines = readFileSync(table, 'utf8').split(/\r?\n/);
  let cases = 0;
  const failures = [];
  lines.forEach((line, i) => {
    if (line.trim() === '' || line.trimStart().startsWith('#')) return;
    cases++;
    const r = m.thinkCase(line);
    assert.ok(r.parsed, `line ${i + 1} does not parse`);
    const expect = line.trim().split(/\s+/).pop();
    const got = r.none ? 'none' : `${r.fromR},${r.fromC}>${r.toR},${r.toC}`;
    if (got !== expect || !r.match) failures.push(`line ${i + 1}: expected ${expect}, got ${got} (score ${r.score.toFixed(3)})`);
  });
  assert.ok(cases >= 30, `only ${cases} cases`);
  assert.deepEqual(failures, []);
});
