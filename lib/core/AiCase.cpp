#include "AiCase.h"
#include <cstdlib>
#include <cstring>

namespace {

// Copies the next space-separated field into buf; returns the position after it or nullptr.
const char* nextField(const char* p, char* buf, int cap) {
  while (*p == ' ' || *p == '\t') p++;
  if (!*p || *p == '\n' || *p == '\r') return nullptr;
  int k = 0;
  while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
    if (k + 1 >= cap) return nullptr;
    buf[k++] = *p++;
  }
  buf[k] = '\0';
  return p;
}

bool parseInt(const char*& p, long& v) {
  char* end;
  v = std::strtol(p, &end, 10);
  if (end == p) return false;
  p = end;
  return true;
}

bool parseFloat(const char*& p, float& v) {
  char* end;
  v = std::strtof(p, &end);
  if (end == p) return false;
  p = end;
  return true;
}

bool expect(const char*& p, char ch) {
  if (*p != ch) return false;
  p++;
  return true;
}

// "r,c"
bool parseSquare(const char*& p, int& r, int& c) {
  long a, b;
  if (!parseInt(p, a) || !expect(p, ',') || !parseInt(p, b)) return false;
  r = static_cast<int>(a);
  c = static_cast<int>(b);
  return true;
}

}  // namespace

bool isAiCaseSkippable(const char* line) {
  const char* p = line;
  while (*p == ' ' || *p == '\t') p++;
  return *p == '\0' || *p == '\n' || *p == '\r' || *p == '#';
}

bool parseAiCase(const char* line, AiCase& out) {
  if (isAiCaseSkippable(line)) return false;
  char f[6][128];
  const char* p = line;
  for (int i = 0; i < 6; i++) {
    p = nextField(p, f[i], sizeof(f[i]));
    if (!p) return false;
  }
  char extra[8];
  if (nextField(p, extra, sizeof(extra))) return false;

  out = AiCase{};
  Snapshot& s = out.snap;
  const int len = static_cast<int>(std::strlen(f[0]));
  int n = 3;
  while (n <= MAX_N && n * n < len) n++;
  if (n > MAX_N || n * n != len) return false;
  s.board.n = n;
  s.clockMs = AI_CASE_CLOCK_MS;
  s.heldIds[0] = s.heldIds[1] = -1;

  // Held origins.
  bool heldAt[MAX_N * MAX_N] = {};
  if (std::strcmp(f[1], "-") != 0) {
    const char* q = f[1];
    for (;;) {
      int r, c;
      if (!parseSquare(q, r, c) || !s.board.inside(r, c) || f[0][r * n + c] != '.') return false;
      heldAt[r * n + c] = true;
      if (*q == '\0') break;
      if (!expect(q, ';')) return false;
    }
  }
  int cooldown[MAX_N * MAX_N] = {};
  if (std::strcmp(f[3], "-") != 0) {
    const char* q = f[3];
    for (;;) {
      int r, c;
      long ms;
      if (!parseSquare(q, r, c) || !s.board.inside(r, c) || !expect(q, '=') || !parseInt(q, ms) || ms < 0)
        return false;
      cooldown[r * n + c] = static_cast<int>(ms);
      if (*q == '\0') break;
      if (!expect(q, ';')) return false;
    }
  }
  for (int i = 0; i < n * n; i++) {
    const char ch = f[0][i];
    if (ch != '.' && ch != 'H' && ch != 'M') return false;
    const bool held = heldAt[i];
    if (ch == '.' && !held) continue;
    if (s.count >= 2 * MAX_N) return false;
    PieceView& pv = s.pieces[s.count];
    pv.id = s.count + 1;
    pv.side = (ch == 'M') ? MACHINE : HUMAN;
    pv.r = i / n;
    pv.c = i % n;
    pv.cooldownUntil = s.clockMs + static_cast<uint32_t>(cooldown[i]);
    pv.alive = true;
    pv.held = held;
    if (held) {
      if (s.heldCount >= 2) return false;
      s.heldIds[s.heldCount++] = pv.id;
    } else {
      s.board.cells[i] = pv.side;
    }
    s.count++;
  }
  {
    const char* q = f[2];
    if (!parseFloat(q, s.headR) || !expect(q, ',') || !parseFloat(q, s.headC) || *q) return false;
  }
  {
    Params& pr = out.params;
    const char* q = f[4];
    long engage, react, readLift;
    if (!parseFloat(q, pr.freeSpeed) || !expect(q, ',') || !parseFloat(q, pr.dragSpeed) || !expect(q, ',') ||
        !parseInt(q, engage) || !expect(q, ',') || !parseInt(q, react) || !expect(q, ','))
      return false;
    switch (*q++) {
      case 'E':
        pr.level = Level::Easy;
        break;
      case 'N':
        pr.level = Level::Normal;
        break;
      case 'H':
        pr.level = Level::Hard;
        break;
      default:
        return false;
    }
    if (!expect(q, ',') || !parseFloat(q, pr.aggression) || !expect(q, ',') || !parseInt(q, readLift) || *q)
      return false;
    if (engage < 0 || react < 0) return false;
    pr.engageMs = static_cast<uint32_t>(engage);
    pr.reactMs = static_cast<uint32_t>(react);
    pr.readLift = readLift != 0;
  }
  if (std::strcmp(f[5], "none") == 0) {
    out.expectNone = true;
    out.fromR = out.fromC = out.toR = out.toC = -1;
  } else {
    const char* q = f[5];
    if (!parseSquare(q, out.fromR, out.fromC) || !expect(q, '>') || !parseSquare(q, out.toR, out.toC) || *q)
      return false;
    out.expectNone = false;
  }
  return true;
}

bool checkAiCase(const AiCase& c, AiMove* got) {
  const AiMove m = think(c.snap, c.params);
  if (got) *got = m;
  if (c.expectNone) return m.none;
  if (m.none || m.pieceId < 1 || m.pieceId > c.snap.count) return false;
  const PieceView& p = c.snap.pieces[m.pieceId - 1];
  return p.r == c.fromR && p.c == c.fromC && m.toR == c.toR && m.toC == c.toC;
}
