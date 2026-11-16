// Builds web/hasami.js (lib/core + wasm/bindings.cpp, WASM embedded) with Emscripten (plan 5.7).
// em++ (emcc's C++ driver: emcc alone does not link libc++, which embind needs) must be on PATH:
// emsdk, version in .emsdk-version.
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdirSync, readdirSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));

// What the build is made of, for the game records (web/record.js): a hash of lib/core and
// wasm/bindings.cpp (names and contents, line endings as LF; first 16 hex digits) and the git
// commit (GITHUB_SHA in CI, otherwise `git rev-parse HEAD`, with "+changes" for a dirty tree).
function coreHash() {
  const h = createHash('sha256');
  const files = readdirSync(`${root}lib/core`)
    .filter((f) => f.endsWith('.cpp') || f.endsWith('.h'))
    .sort()
    .map((f) => `lib/core/${f}`);
  files.push('wasm/bindings.cpp');
  for (const f of files) {
    const data = readFileSync(`${root}${f}`, 'utf8').replace(/\r\n/g, '\n');
    h.update(`${f}\0${data.length}\0${data}`);
  }
  return h.digest('hex').slice(0, 16);
}
function commit() {
  if (process.env.GITHUB_SHA) return process.env.GITHUB_SHA;
  const head = spawnSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' });
  if (head.status !== 0) return 'unknown';
  const dirty = spawnSync('git', ['status', '--porcelain', '--', 'lib/core', 'wasm'], { cwd: root, encoding: 'utf8' });
  return head.stdout.trim() + (dirty.stdout.trim() ? '+changes' : '');
}

const sources = readdirSync(`${root}lib/core`)
  .filter((f) => f.endsWith('.cpp'))
  .sort()
  .map((f) => `lib/core/${f}`);
sources.push('wasm/bindings.cpp');

const args = [
  '-std=c++17',
  '-O2',
  '-ffp-contract=off', // same floating point as the native and ESP32 builds (platformio.ini)
  '-Ilib/core',
  `-DHASAMI_CORE_HASH="${coreHash()}"`,
  `-DHASAMI_COMMIT="${commit()}"`,
  '-lembind',
  '-sMODULARIZE',
  '-sEXPORT_ES6',
  '-sENVIRONMENT=web,node',
  '-sSINGLE_FILE',
  '-sALLOW_MEMORY_GROWTH',
  ...sources,
  '-o',
  'web/hasami.js',
];

mkdirSync(`${root}web`, { recursive: true });

// A .bat cannot be started without a shell; go through cmd.exe rather than `shell: true` with
// an argument array (DEP0190).
const res =
  process.platform === 'win32'
    ? spawnSync('cmd.exe', ['/c', 'em++.bat', ...args], { cwd: root, stdio: 'inherit' })
    : spawnSync('em++', args, { cwd: root, stdio: 'inherit' });

if (res.error) {
  console.error(`build-wasm: cannot run em++ (${res.error.message}).`);
  console.error('Install emsdk outside the repository and activate the version in .emsdk-version:');
  console.error('  ./emsdk install <version> && ./emsdk activate <version> && source ./emsdk_env.sh');
  process.exit(1);
}
if (res.status !== 0) {
  console.error(`build-wasm: em++ failed (${res.status ?? res.signal})`);
  process.exit(1);
}
console.log('build-wasm: wrote web/hasami.js');
