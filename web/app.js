// Screen of the simulator (ported from sim/hasami-sim.html). All game logic lives in the WASM
// SimDriver (lib/core); this file only draws state() and forwards clicks and settings.
import createModule from './hasami.js';

const m = await createModule();
const { Phase, Winner, Level, PlaceResult, EventKind } = m;

const HUMAN = 1;
const KAN = ['一', '二', '三', '四', '五', '六', '七', '八', '九'];
const $ = (id) => document.getElementById(id);
const boardEl = $('board'), cellsEl = $('cells'), layerEl = $('layer'), overlayEl = $('overlay');

// Settings start from the C++ defaults (no second copy of them here).
const D = m.defaults();
const rules = { ...D.rules };
const params = { ...D.params };
const sim = new m.SimDriver(rules, params, (Date.now() >>> 0) || 1);

let S = sim.state();
let N = S.n;
let activeRules = { ...rules }; // rules of the game on the board (setRules applies at reset)
let cellEls = [], komaEls = new Map(), headEl = null;
let carried = new Set(); // captured pieces the machine has started carrying off
let log = [], lastSeq = 0, logDirty = true;
let overlayKind = undefined;
let countdownLeft = 0, prevPhase = S.phase;

const sq = (r, c) => `${N - c}${KAN[r]}`;
const is = (a, b) => a === b; // embind enum values are singletons

/* ---------- loop ---------- */
let last = performance.now(), acc = 0;
function frame(now) {
  const dt = Math.min(50, now - last);
  last = now;
  acc += dt;
  const step = Math.floor(acc);
  acc -= step;
  if (step > 0) sim.advance(step);
  S = sim.state();
  if (is(S.phase, Phase.Countdown)) {
    if (!is(prevPhase, Phase.Countdown)) {
      const fresh = is(prevPhase, Phase.Ready) || is(prevPhase, Phase.Over);
      countdownLeft = fresh ? activeRules.countdownMs : activeRules.resumeCountdownMs;
    } else {
      countdownLeft = Math.max(0, countdownLeft - step);
    }
  }
  prevPhase = S.phase;
  render();
  requestAnimationFrame(frame);
}

/* ---------- human input ---------- */
function cellFromEvent(e) {
  const r = boardEl.getBoundingClientRect();
  const c = Math.floor(((e.clientX - r.left) / r.width) * N), rr = Math.floor(((e.clientY - r.top) / r.height) * N);
  if (c < 0 || c >= N || rr < 0 || rr >= N) return null;
  return [rr, c];
}
const heldPiece = () => S.pieces.find((p) => p.held && p.side === HUMAN) || null;

// Drag and drop: pressing a piece lifts it; releasing on another square places it there, releasing
// off the board or on a square it cannot go to puts it back (a cancel). Releasing on the same square
// without moving leaves it held, so click-to-lift / click-to-place still works.
let drag = null; // { id, pointerId, r, c, moved, x, y } with x, y in squares (piece's top-left)
function dragPos(e) {
  const b = boardEl.getBoundingClientRect();
  return [((e.clientX - b.left) / b.width) * N - 0.5, ((e.clientY - b.top) / b.height) * N - 0.5];
}
function endDrag() {
  if (drag && boardEl.hasPointerCapture(drag.pointerId)) boardEl.releasePointerCapture(drag.pointerId);
  drag = null;
  boardEl.classList.remove('dragging');
}
boardEl.addEventListener('pointermove', (e) => {
  if (!drag || e.pointerId !== drag.pointerId) return;
  [drag.x, drag.y] = dragPos(e);
  const cell = cellFromEvent(e);
  if (!cell || cell[0] !== drag.r || cell[1] !== drag.c) drag.moved = true;
});
function drop(e, cancelled) {
  if (!drag || e.pointerId !== drag.pointerId) return;
  const { id, moved } = drag;
  endDrag();
  const h = heldPiece();
  if (!moved || !h || h.id !== id) return;
  const cell = cancelled ? null : cellFromEvent(e);
  const res = cell ? sim.place(cell[0], cell[1]) : null;
  if (!res || is(res, PlaceResult.Illegal)) sim.place(h.r, h.c);
  S = sim.state();
}
boardEl.addEventListener('pointerup', (e) => drop(e, false));
boardEl.addEventListener('pointercancel', (e) => drop(e, true));

boardEl.addEventListener('pointerdown', (e) => {
  if (drag) return;
  const cell = cellFromEvent(e);
  if (!cell) return;
  const [r, c] = cell;
  const h = heldPiece();
  if (!h) {
    sim.lift(r, c);
  } else {
    // place() on the origin square cancels the lift (SimDriver/Game decide).
    const res = sim.place(r, c);
    // As in the original: clicking another of your pieces that is not cooling down switches to it.
    const other = S.pieces.find((q) => q.alive && !q.held && q.side === HUMAN && q.r === r && q.c === c);
    if (is(res, PlaceResult.Illegal) && other && other.cooldownLeft <= 0) {
      const back = sim.place(h.r, h.c);
      if (is(back, PlaceResult.Ok) || is(back, PlaceResult.Cancelled)) sim.lift(r, c);
    }
  }
  S = sim.state();
  // Whatever got lifted under the pointer can be dragged from here.
  const now = heldPiece();
  if (now && now.r === r && now.c === c) {
    drag = { id: now.id, pointerId: e.pointerId, r, c, moved: false, x: c, y: r };
    [drag.x, drag.y] = dragPos(e);
    boardEl.setPointerCapture(e.pointerId);
    boardEl.classList.add('dragging');
    e.preventDefault();
  }
});
document.addEventListener('keydown', (e) => {
  if (e.key !== 'Escape') return;
  const h = heldPiece();
  if (h) {
    sim.place(h.r, h.c);
    S = sim.state();
  }
});

/* ---------- game control ---------- */
function resetGame() {
  sim.reset();
  afterReset();
}
// A new game is on the board (reset, or start() from Ready/Over): rebuild the view.
function afterReset() {
  activeRules = { ...rules };
  log = []; // as in the original, a new game starts with an empty record
  lastSeq = 0;
  logDirty = true;
  S = sim.state();
  prevPhase = S.phase;
  build();
}
function start() {
  const ph = sim.phase();
  if (is(ph, Phase.Ready) || is(ph, Phase.Over)) {
    sim.start(); // resets inside
    const was = prevPhase;
    afterReset();
    prevPhase = was; // keep Ready/Over so the countdown length is the fresh one
  } else {
    // As in the original: start during a game begins a new one.
    sim.reset();
    afterReset();
    sim.start();
  }
  S = sim.state();
}
function togglePause() {
  sim.togglePause();
}

/* ---------- view ---------- */
function build() {
  N = S.n;
  cellsEl.style.gridTemplateColumns = `repeat(${N},1fr)`;
  cellsEl.style.gridTemplateRows = `repeat(${N},1fr)`;
  cellsEl.innerHTML = '';
  cellEls = [];
  for (let r = 0; r < N; r++)
    for (let c = 0; c < N; c++) {
      const d = document.createElement('div');
      d.className = 'cell' + (c === N - 1 ? ' lr' : '') + (r === N - 1 ? ' lb' : '');
      d.setAttribute('role', 'gridcell');
      d.setAttribute('aria-label', sq(r, c));
      const bar = document.createElement('i');
      bar.className = 'bar';
      const s = document.createElement('span');
      s.className = 'sec';
      d.append(bar, s);
      cellsEl.append(d);
      cellEls.push({ el: d, sec: s, key: '' });
    }
  layerEl.innerHTML = '';
  komaEls.clear();
  carried = new Set();
  headEl = document.createElement('div');
  headEl.className = 'head';
  headEl.style.width = headEl.style.height = `${100 / N}%`;
  layerEl.append(headEl);
  for (const p of S.pieces) {
    const k = document.createElement('div');
    k.className = 'koma ' + (p.side === HUMAN ? 'h' : 'm');
    k.style.width = k.style.height = `${100 / N}%`;
    k.innerHTML = `<div class="edge"><div class="shape">${p.side === HUMAN ? '歩' : 'と'}</div></div>`;
    layerEl.append(k);
    komaEls.set(p.id, k);
  }
  $('files').style.gridTemplateColumns = `repeat(${N},1fr)`;
  $('files').innerHTML = Array.from({ length: N }, (_, c) => `<span>${N - c}</span>`).join('');
  $('ranks').style.gridTemplateRows = `repeat(${N},1fr)`;
  $('ranks').innerHTML = Array.from({ length: N }, (_, r) => `<span>${KAN[r]}</span>`).join('');
  overlayKind = undefined;
  sizeCells();
}
function sizeCells() {
  const w = boardEl.getBoundingClientRect().width;
  boardEl.style.setProperty('--cell', w / N + 'px');
}
new ResizeObserver(sizeCells).observe(boardEl);

// LED color (0xRRGGBB from LedPlan) -> the theme's color for that kind of light and its brightness.
const LED_KINDS = [
  [0x0000ff, 'blue', 'var(--blue)'],
  [0xa000ff, 'purple', 'var(--cyan)'],
  [0xffc800, 'yellow', 'var(--red)'],
  [0xffffff, 'white', 'var(--white-led)'],
  [0xff0000, 'red', 'var(--amber)'],
];
function ledLook(v) {
  if (!v) return null;
  const r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255;
  const mx = Math.max(r, g, b);
  // Normalize to full brightness, then match the hue (cooldown red is scaled by the time left).
  const full = (Math.round((r * 255) / mx) << 16) | (Math.round((g * 255) / mx) << 8) | Math.round((b * 255) / mx);
  for (const [rgb, kind, css] of LED_KINDS) if (rgb === full) return { kind, css, glow: mx / 255 };
  return { kind: 'other', css: `rgb(${(full >> 16) & 255},${(full >> 8) & 255},${full & 255})`, glow: mx / 255 };
}

function render() {
  // The game may drop the held piece mid-drag (Esc, reset, phase change).
  if (drag) {
    const h = heldPiece();
    if (!h || h.id !== drag.id) endDrag();
  }
  const job = S.job;
  const jobPiece = job.active ? job.pieceId : -1;
  const secs = new Array(N * N).fill(''), cdFrac = new Float32Array(N * N);
  let origin = -1;
  for (const p of S.pieces) {
    if (p.held && p.side === HUMAN) origin = p.r * N + p.c;
    if (!p.alive || p.held || p.id === jobPiece || p.cooldownLeft <= 0) continue;
    const i = p.r * N + p.c;
    const len = p.side === HUMAN ? activeRules.humanCooldownMs : activeRules.machineCooldownMs;
    secs[i] = (p.cooldownLeft / 1000).toFixed(1);
    cdFrac[i] = Math.min(1, p.cooldownLeft / Math.max(1, len));
  }
  for (let i = 0; i < N * N; i++) {
    const ce = cellEls[i];
    if (!ce) continue;
    const led = S.leds[i];
    const key = `${led}|${secs[i]}|${cdFrac[i].toFixed(2)}|${i === origin}`;
    if (key === ce.key) continue;
    ce.key = key;
    const look = ledLook(led);
    if (look) {
      ce.el.style.setProperty('--led', look.css);
      ce.el.style.setProperty('--glow', look.glow.toFixed(3));
      ce.el.dataset.led = look.kind;
    } else {
      ce.el.style.setProperty('--glow', 0);
      delete ce.el.dataset.led;
    }
    ce.el.style.setProperty('--cd', cdFrac[i]);
    ce.sec.textContent = secs[i];
    ce.el.classList.toggle('origin', i === origin);
  }
  const removing = is(S.phase, Phase.Removing) || is(S.phase, Phase.Paused);
  for (const p of S.pieces) {
    const k = komaEls.get(p.id);
    if (!k) continue;
    if (!p.alive) {
      // A captured piece stays on its square until the machine carries it off.
      const beingCarried = job.active && job.removing && job.pieceId === p.id;
      if (beingCarried) carried.add(p.id);
      const waiting = removing && !carried.has(p.id);
      if (!beingCarried && !waiting) {
        if (!k.classList.contains('gone')) {
          k.classList.add('gone');
          setTimeout(() => k.remove(), 550);
        }
        continue;
      }
      k.classList.add('dead');
    }
    const dragging = drag !== null && drag.moved && p.id === drag.id && p.held;
    const x = dragging ? drag.x : p.x, y = dragging ? drag.y : p.y;
    k.style.transform = `translate(${x * 100}%,${y * 100}%)`;
    k.classList.toggle('dragging', dragging);
    k.classList.toggle('held', p.held);
    k.classList.toggle('cool', p.alive && p.id !== jobPiece && p.cooldownLeft > 0 && !p.held);
  }
  headEl.style.transform = `translate(${S.headC * 100}%,${S.headR * 100}%)`;
  headEl.classList.toggle('busy', job.active);
  $('capH').innerHTML = `${S.capturedHuman}<small> / ${activeRules.target}</small>`;
  $('capM').innerHTML = `${S.capturedMachine}<small> / ${activeRules.target}</small>`;
  $('clock').textContent = fmt(S.clockMs);
  $('saves').textContent = S.saves;
  $('mmoves').textContent = S.machineMoves;
  $('status').textContent = statusText();
  updateLog();
  if (logDirty) renderLog();
  updateOverlay();
}
function fmt(ms) {
  const s = ms / 1000;
  return `${Math.floor(s / 60)}:${(s % 60).toFixed(1).padStart(4, '0')}`;
}
function statusText() {
  const ph = S.phase;
  if (is(ph, Phase.Ready)) return '「対局開始」を押すと3秒後に始まります。';
  if (is(ph, Phase.Countdown)) return 'まもなく開始します。';
  if (is(ph, Phase.Paused)) return '一時停止中です。';
  if (is(ph, Phase.Removing)) return '駒が取られたので、機械が盤外へ運び出しています。';
  if (is(ph, Phase.Over)) return is(S.winner, Winner.Human) ? 'あなたの勝ちです。' : '機械の勝ちです。';
  const h = heldPiece();
  if (h) return `${sq(h.r, h.c)}の歩を持ち上げています。置くマスで離すかクリック、元のマスで取り消し。`;
  return '自分の歩をドラッグして動かします（クリックでも可）。';
}

/* ---------- log ---------- */
// state().events is newest first and numbered by seq; add the ones not seen yet.
function updateLog() {
  const ev = S.events;
  if (ev.length === 0 ? lastSeq > 0 : ev[0].seq < lastSeq) {
    log = []; // the driver was reset
    lastSeq = 0;
    logDirty = true;
  }
  for (let i = ev.length - 1; i >= 0; i--) {
    const e = ev[i];
    if (e.seq <= lastSeq) continue;
    lastSeq = e.seq;
    addLog(e);
  }
}
function addLog(e) {
  let text, kind;
  if (is(e.kind, EventKind.Save)) {
    text = `先回り成功：${sq(e.r, e.c)}への狙いを読んでふさいだ`;
    kind = 'read';
  } else if (e.side === HUMAN) {
    text = `あなたが${sq(e.r, e.c)}に置いて${e.count}枚取り`;
    kind = 'h cap';
  } else {
    text = e.r >= 0 ? `機械が${sq(e.r, e.c)}に動いて${e.count}枚取り` : `機械が${e.count}枚取り`;
    kind = 'm cap';
  }
  log.unshift({ t: e.clockMs, text, kind });
  if (log.length > 40) log.pop();
  logDirty = true;
}
function renderLog() {
  logDirty = false;
  const el = $('log');
  if (!log.length) {
    el.innerHTML = '<li class="empty">対局が始まると、取った駒と機械の先回りがここに残ります。</li>';
    return;
  }
  el.innerHTML = log.map((l) => `<li><time>${fmt(l.t)}</time><span class="${l.kind}">${l.text}</span></li>`).join('');
}

/* ---------- overlay ---------- */
function lastCaptureCount() {
  const e = S.events.find((x) => is(x.kind, EventKind.Capture));
  return e ? e.count : 0;
}
function updateOverlay() {
  const ph = S.phase;
  let kind = null;
  if (is(ph, Phase.Ready)) kind = 'ready';
  else if (is(ph, Phase.Countdown)) kind = 'countdown';
  else if (is(ph, Phase.Paused)) kind = 'paused';
  else if (is(ph, Phase.Removing)) kind = 'removing';
  else if (is(ph, Phase.Over)) kind = 'over';
  if (kind !== overlayKind) {
    overlayKind = kind;
    showOverlay(kind, lastCaptureCount());
  }
  if (kind === 'countdown') setCountdown(Math.max(1, Math.ceil(countdownLeft / 1000)));
}
function showOverlay(kind, n) {
  if (!kind) {
    overlayEl.hidden = true;
    return;
  }
  overlayEl.hidden = false;
  if (kind === 'ready')
    overlayEl.innerHTML = `<div class="card"><div class="big">対局開始</div><p>ターンはありません。歩をつかんで、好きなときに動かしてください。</p><button class="btn" id="ovStart">開始</button></div>`;
  if (kind === 'countdown') overlayEl.innerHTML = `<div class="card"><div class="big" id="cdNum">3</div></div>`;
  if (kind === 'removing') overlayEl.innerHTML = `<div class="card"><div class="big">回収中</div><p>${n}枚を盤外へ運び出しています</p></div>`;
  if (kind === 'paused') overlayEl.innerHTML = `<div class="card"><div class="big">一時停止</div><button class="btn" id="ovResume">再開</button></div>`;
  if (kind === 'over')
    overlayEl.innerHTML = `<div class="card"><div class="big">${is(S.winner, Winner.Human) ? 'あなたの勝ち' : '機械の勝ち'}</div><p>${S.capturedHuman} 対 ${S.capturedMachine}・${fmt(S.clockMs)}・先回り成功 ${S.saves}回</p><button class="btn" id="ovAgain">もう一局</button></div>`;
  const s = $('ovStart');
  if (s) s.onclick = start;
  const rs = $('ovResume');
  if (rs) rs.onclick = togglePause;
  const a = $('ovAgain');
  if (a) a.onclick = start;
}
function setCountdown(n) {
  const e = $('cdNum');
  if (e && e.textContent !== String(n)) e.textContent = n;
}

/* ---------- settings ---------- */
$('startBtn').onclick = start;
$('pauseBtn').onclick = togglePause;

// Board size and target: setRules, then reset. Other rules apply from the next game.
function applyRules(resetNow) {
  sim.setRules(rules);
  if (resetNow || is(sim.phase(), Phase.Ready)) resetGame();
}
function setN(n) {
  rules.n = n;
  rules.target = n === 7 ? 4 : 5;
  $('target').value = rules.target;
  $('n7').setAttribute('aria-pressed', n === 7);
  $('n9').setAttribute('aria-pressed', n === 9);
  applyRules(true);
}
$('n7').onclick = () => setN(7);
$('n9').onclick = () => setN(9);
$('n7').setAttribute('aria-pressed', rules.n === 7);
$('n9').setAttribute('aria-pressed', rules.n === 9);
$('target').value = rules.target;
$('target').oninput = (e) => {
  const v = parseInt(e.target.value, 10);
  if (v >= 1 && v <= 9) {
    rules.target = v;
    applyRules(true);
  }
};
$('diffOn').checked = rules.diffOn;
$('diffOn').onchange = (e) => {
  rules.diffOn = e.target.checked;
  applyRules(false);
};
$('diff').value = rules.diff;
$('diff').oninput = (e) => {
  const v = parseInt(e.target.value, 10);
  if (v >= 1) {
    rules.diff = v;
    applyRules(false);
  }
};

// A range input bound to a setting: init from the current value, write back on input.
function bindRange(id, get, set, fmtF) {
  const el = $(id), out = $(id + 'Out');
  el.value = get();
  const f = () => {
    const v = parseFloat(el.value);
    set(v);
    out.textContent = fmtF(v);
  };
  el.oninput = f;
  out.textContent = fmtF(parseFloat(el.value));
}
const applyParams = () => sim.setParams(params);
bindRange('hCd', () => rules.humanCooldownMs / 1000, (v) => { rules.humanCooldownMs = Math.round(v * 1000); applyRules(false); }, (v) => v.toFixed(1) + ' 秒');
bindRange('mCd', () => rules.machineCooldownMs / 1000, (v) => { rules.machineCooldownMs = Math.round(v * 1000); applyRules(false); }, (v) => v.toFixed(1) + ' 秒');
const mm = (v) => `${+v.toFixed(2)} マス/秒（${Math.round(v * 30)} mm/s）`;
bindRange('speed', () => params.dragSpeed, (v) => { params.dragSpeed = v; applyParams(); }, mm);
bindRange('free', () => params.freeSpeed, (v) => { params.freeSpeed = v; applyParams(); }, mm);
bindRange('react', () => params.reactMs, (v) => { params.reactMs = Math.round(v); applyParams(); }, (v) => v + ' ms');
bindRange('aggr', () => params.aggression, (v) => { params.aggression = v; applyParams(); }, (v) => Math.round(v * 100) + ' %');
$('read').checked = params.readLift;
$('read').onchange = (e) => {
  params.readLift = e.target.checked;
  applyParams();
};
const lv = [[Level.Easy, 'lvEasy'], [Level.Normal, 'lvNormal'], [Level.Hard, 'lvHard']];
const showLevel = () => { for (const [l, id] of lv) $(id).setAttribute('aria-pressed', is(params.level, l)); };
for (const [l, id] of lv)
  $(id).onclick = () => {
    params.level = l;
    applyParams();
    showLevel();
  };
showLevel();

build();
requestAnimationFrame(frame);
