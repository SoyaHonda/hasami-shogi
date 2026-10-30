// Builds web/hasami.js (lib/core + wasm/bindings.cpp, WASM embedded) with Emscripten (plan 5.7).
// em++ (emcc's C++ driver: emcc alone does not link libc++, which embind needs) must be on PATH:
// emsdk, version in .emsdk-version.
import { spawnSync } from 'node:child_process';
import { mkdirSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));

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
