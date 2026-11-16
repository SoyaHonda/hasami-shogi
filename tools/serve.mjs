// Static server for web/ on http://localhost:8080 (ES modules cannot be loaded from file://).
// Node.js standard library only. Usage: npm run serve  (PORT=... to change the port)
// POST /api/games takes a game record from the page (web/record.js) and appends it, one JSON
// object per line, to logs/games.jsonl (not in git).
import { createServer } from 'node:http';
import { appendFile, mkdir, readFile, stat } from 'node:fs/promises';
import { extname, join, normalize, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../web/', import.meta.url));
const logs = fileURLToPath(new URL('../logs/', import.meta.url));
const port = Number(process.env.PORT) || 8080;

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.wasm': 'application/wasm',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
};

function send(res, code, body, type = 'text/plain; charset=utf-8') {
  res.writeHead(code, { 'Content-Type': type, 'Cache-Control': 'no-store' });
  res.end(body);
}

// A record from the page: checked to be one JSON object of the record format, then appended.
async function saveGame(req, res) {
  let body = '';
  for await (const chunk of req) {
    body += chunk;
    if (body.length > 20 * 1024 * 1024) return send(res, 413, 'Too Large');
  }
  let rec;
  try {
    rec = JSON.parse(body);
  } catch {
    return send(res, 400, 'Bad JSON');
  }
  if (!rec || rec.format !== 1 || !Array.isArray(rec.ops)) return send(res, 400, 'Not a game record');
  await mkdir(logs, { recursive: true });
  await appendFile(join(logs, 'games.jsonl'), JSON.stringify(rec) + '\n');
  send(res, 204, '');
}

const server = createServer(async (req, res) => {
  if (req.method === 'POST' && req.url === '/api/games') return saveGame(req, res);
  if (req.method !== 'GET' && req.method !== 'HEAD') return send(res, 405, 'Method Not Allowed');
  let path;
  try {
    path = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
  } catch {
    return send(res, 400, 'Bad Request');
  }
  if (path.endsWith('/')) path += 'index.html';
  const file = normalize(join(root, path));
  if (!file.startsWith(root) && file + sep !== root) return send(res, 403, 'Forbidden');
  try {
    const st = await stat(file);
    if (!st.isFile()) return send(res, 404, 'Not Found');
    const body = await readFile(file);
    res.writeHead(200, {
      'Content-Type': TYPES[extname(file).toLowerCase()] || 'application/octet-stream',
      'Content-Length': body.length,
      'Cache-Control': 'no-store',
    });
    res.end(req.method === 'HEAD' ? undefined : body);
  } catch {
    send(res, 404, 'Not Found');
  }
});

server.listen(port, () => {
  console.log(`Serving ${root} at http://localhost:${port}/`);
});
