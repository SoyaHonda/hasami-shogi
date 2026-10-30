// Loads the WASM build of lib/core (web/hasami.js, made by `npm run build:wasm`).
import { existsSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';

const modulePath = fileURLToPath(new URL('../web/hasami.js', import.meta.url));

export async function loadHasami() {
  if (!existsSync(modulePath)) {
    throw new Error(`${modulePath} not found: run \`npm run build:wasm\` first`);
  }
  const { default: createModule } = await import(pathToFileURL(modulePath).href);
  return createModule();
}
