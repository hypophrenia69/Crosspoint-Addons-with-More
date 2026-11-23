#pragma once
// Logic of the two games (added by the Crosspoint-Apps patch): Sudoku and Knucklebones.
// Pure C++, no firmware headers, so it is tested on a PC (test/test_games.cpp).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace capps {

// Small random generator (xorshift32); the seed comes from the clock.
struct Rng {
  uint32_t s = 2463534242u;
  void seed(uint32_t v) { s = v ? v : 2463534242u; next(); next(); }
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  int below(int n) { return static_cast<int>(next() % static_cast<uint32_t>(n)); }
};

// =================================================================== Knucklebones
// Two 3x3 boards. On your turn you roll a d6 and put it into a column of YOUR board that has room.
// A column scores the sum of its dice, equal dice multiply (two of a kind count double each, three
// triple each). The placed die destroys all dice of the same value in the opposite column of the
// other board. The game ends when one board is full; the higher total wins.

constexpr int KB_COLS = 3;
constexpr int KB_ROWS = 3;

struct KBoard {
  uint8_t d[KB_COLS][KB_ROWS] = {};  // d[col][slot], slot 0 is nearest the centre; 0 = empty, packed

  int count(int c) const {
    int n = 0;
    for (int s = 0; s < KB_ROWS; ++s) n += d[c][s] != 0;
    return n;
  }
  bool hasRoom(int c) const { return count(c) < KB_ROWS; }
  int colScore(int c) const {
    int total = 0;
    for (int s = 0; s < KB_ROWS; ++s) {
      const int v = d[c][s];
      if (!v) continue;
      int same = 0;
      for (int t = 0; t < KB_ROWS; ++t) same += d[c][t] == v;
      total += v * same;
    }
    return total;
  }
  int score() const {
    int t = 0;
    for (int c = 0; c < KB_COLS; ++c) t += colScore(c);
    return t;
  }
  bool full() const {
    for (int c = 0; c < KB_COLS; ++c)
      if (hasRoom(c)) return false;
    return true;
  }
  // Returns the slot used, or -1 when the column is full.
  int place(int c, int v) {
    const int n = count(c);
    if (n >= KB_ROWS) return -1;
    d[c][n] = static_cast<uint8_t>(v);
    return n;
  }
  // Removes all dice of value v from column c and packs the rest; returns how many were removed.
  int smash(int c, int v) {
    uint8_t keep[KB_ROWS] = {};
    int k = 0;
    int removed = 0;
    for (int s = 0; s < KB_ROWS; ++s) {
      const int x = d[c][s];
      if (x == v) ++removed;
      else if (x) keep[k++] = static_cast<uint8_t>(x);
    }
    for (int s = 0; s < KB_ROWS; ++s) d[c][s] = keep[s];
    return removed;
  }
};

namespace kbai {

// Expected score difference (from "me") with the mover about to roll and move.
inline double value(const KBoard& me, const KBoard& op, bool moverIsMe, int depth) {
  if (depth == 0 || me.full() || op.full()) return me.score() - op.score();
  double sum = 0;
  for (int v = 1; v <= 6; ++v) {
    bool any = false;
    double best = 0;
    for (int c = 0; c < KB_COLS; ++c) {
      const KBoard& mover = moverIsMe ? me : op;
      if (!mover.hasRoom(c)) continue;
      KBoard m = me, t = op;
      double val;
      if (moverIsMe) {
        m.place(c, v);
        t.smash(c, v);
        val = value(m, t, false, depth - 1);
      } else {
        t.place(c, v);
        m.smash(c, v);
        val = value(m, t, true, depth - 1);
      }
      if (!any || (moverIsMe && val > best) || (!moverIsMe && val < best)) best = val;
      any = true;
    }
    sum += any ? best : (me.score() - op.score());
  }
  return sum / 6;
}

}  // namespace kbai

// difficulty 0 = easy (random), 1 = medium (greedy), 2 = hard (3 plies of expectimax).
inline int kbChoose(int difficulty, const KBoard& mine, const KBoard& theirs, int v, Rng& rng) {
  int opts[KB_COLS];
  int n = 0;
  double best = 0;
  for (int c = 0; c < KB_COLS; ++c) {
    if (!mine.hasRoom(c)) continue;
    double val = 0;
    if (difficulty == 1) {
      KBoard m = mine, t = theirs;
      m.place(c, v);
      t.smash(c, v);
      val = m.score() - t.score();
    } else if (difficulty >= 2) {
      KBoard m = mine, t = theirs;
      m.place(c, v);
      t.smash(c, v);
      val = kbai::value(m, t, false, 2);
    }
    if (difficulty == 0) {
      opts[n++] = c;
    } else if (n == 0 || val > best + 1e-9) {
      best = val;
      n = 0;
      opts[n++] = c;
    } else if (val > best - 1e-9) {
      opts[n++] = c;
    }
  }
  return n ? opts[rng.below(n)] : -1;
}

struct KBMove {
  int player = 0;  // 1 = bottom board, 2 = top board / computer
  int col = -1;
  int slot = -1;
  int die = 0;
  int smashed = 0;
};

class Knucklebones {
 public:
  KBoard b1, b2;       // b1 = bottom (player 1), b2 = top (player 2 / computer)
  bool vsCpu = true;
  int difficulty = 1;  // 0..2
  int turn = 1;        // 1 or 2
  int die = 1;         // the die that waits to be placed
  bool over = false;
  int wins1 = 0, wins2 = 0, draws = 0;
  Rng rng;

  void newGame(bool cpu, int diff, uint32_t seed) {
    rng.seed(seed);
    b1 = KBoard();
    b2 = KBoard();
    vsCpu = cpu;
    difficulty = diff < 0 ? 0 : (diff > 2 ? 2 : diff);
    turn = 1;
    over = false;
    die = roll();
  }

  int roll() { return 1 + rng.below(6); }

  // 1 = player 1 wins, 2 = player 2 / computer, 0 = draw. Only meaningful once over.
  int winner() const {
    const int a = b1.score();
    const int b = b2.score();
    return a > b ? 1 : (b > a ? 2 : 0);
  }

  // The active player puts the waiting die in column c. Returns false for an illegal move.
  bool play(int c, KBMove& out) {
    if (over || c < 0 || c >= KB_COLS) return false;
    KBoard& mine = turn == 1 ? b1 : b2;
    KBoard& theirs = turn == 1 ? b2 : b1;
    if (!mine.hasRoom(c)) return false;
    out.player = turn;
    out.col = c;
    out.die = die;
    out.slot = mine.place(c, die);
    out.smashed = theirs.smash(c, die);
    finishIfOver();
    if (!over) {
      turn = 3 - turn;
      die = roll();
    }
    return true;
  }

  // True when it is the computer's turn.
  bool cpuToMove() const { return vsCpu && !over && turn == 2; }

  // The computer plays its turn.
  bool playCpu(KBMove& out) {
    if (!cpuToMove()) return false;
    const int c = kbChoose(difficulty, b2, b1, die, rng);
    return play(c, out);
  }

  std::string serialize() const {
    char buf[96];
    int n = snprintf(buf, sizeof(buf), "K1 %d %d %d %d %d %d %d %d ", vsCpu ? 1 : 0, difficulty, turn, die,
                     over ? 1 : 0, wins1, wins2, draws);
    for (int c = 0; c < KB_COLS; ++c)
      for (int s = 0; s < KB_ROWS; ++s) buf[n++] = static_cast<char>('0' + b1.d[c][s]);
    for (int c = 0; c < KB_COLS; ++c)
      for (int s = 0; s < KB_ROWS; ++s) buf[n++] = static_cast<char>('0' + b2.d[c][s]);
    buf[n++] = '\n';
    buf[n] = 0;
    return buf;
  }

  // Returns false when the text is not a valid saved game.
  bool parse(const std::string& text) {
    int cpu, diff, t, dd, ov, w1, w2, dr;
    char cells[40] = {};
    if (sscanf(text.c_str(), "K1 %d %d %d %d %d %d %d %d %36s", &cpu, &diff, &t, &dd, &ov, &w1, &w2, &dr,
               cells) != 9) {
      return false;
    }
    if (strlen(cells) != 18 || diff < 0 || diff > 2 || (t != 1 && t != 2) || dd < 1 || dd > 6) return false;
    Knucklebones g;
    g.vsCpu = cpu != 0;
    g.difficulty = diff;
    g.turn = t;
    g.die = dd;
    g.over = ov != 0;
    g.wins1 = w1;
    g.wins2 = w2;
    g.draws = dr;
    for (int i = 0; i < 18; ++i) {
      const int v = cells[i] - '0';
      if (v < 0 || v > 6) return false;
      KBoard& b = i < 9 ? g.b1 : g.b2;
      const int k = i % 9;
      b.d[k / KB_ROWS][k % KB_ROWS] = static_cast<uint8_t>(v);
    }
    // Dice must be packed toward the centre.
    for (const KBoard* b : {&g.b1, &g.b2}) {
      for (int c = 0; c < KB_COLS; ++c)
        for (int s = 1; s < KB_ROWS; ++s)
          if (b->d[c][s] && !b->d[c][s - 1]) return false;
    }
    g.rng = rng;
    *this = g;
    return true;
  }

 private:
  void finishIfOver() {
    if (!b1.full() && !b2.full()) return;
    over = true;
    const int w = winner();
    if (w == 1) ++wins1;
    else if (w == 2) ++wins2;
    else ++draws;
  }
};

// ======================================================================= Sudoku

enum class SudokuLevel : uint8_t { Easy, Medium, Hard };

class Sudoku {
 public:
  uint8_t given[81] = {};  // the clues of the puzzle (0 = empty)
  uint8_t sol[81] = {};    // the one solution
  uint8_t val[81] = {};    // what the board shows now (clues + the player's digits)
  uint16_t notes[81] = {}; // pencil marks, bit n = digit n (not saved)
  SudokuLevel level = SudokuLevel::Easy;
  int hints = 0;
  bool valid = false;

  static int clueTarget(SudokuLevel l) { return l == SudokuLevel::Easy ? 40 : (l == SudokuLevel::Medium ? 32 : 26); }

  // Creates a new puzzle with exactly one solution.
  void generate(SudokuLevel lv, uint32_t seed) {
    Rng rng;
    rng.seed(seed);
    level = lv;
    hints = 0;
    memset(notes, 0, sizeof(notes));
    // 1) a full random grid
    uint8_t g[81] = {};
    for (int attempt = 0; attempt < 20; ++attempt) {
      memset(g, 0, sizeof(g));
      long budget = 200000;
      if (fill(g, rng, budget)) break;
    }
    memcpy(sol, g, 81);
    // 2) remove clues one by one while the solution stays unique
    int order[81];
    for (int i = 0; i < 81; ++i) order[i] = i;
    for (int i = 80; i > 0; --i) {
      const int j = rng.below(i + 1);
      const int t = order[i];
      order[i] = order[j];
      order[j] = t;
    }
    int clues = 81;
    const int target = clueTarget(lv);
    for (int k = 0; k < 81 && clues > target; ++k) {
      const int i = order[k];
      const uint8_t keep = g[i];
      g[i] = 0;
      long nodes = 20000;  // too hard to decide quickly -> keep the clue
      if (countSolutions(g, 2, nodes) != 1 || nodes <= 0) g[i] = keep;
      else --clues;
    }
    memcpy(given, g, 81);
    memcpy(val, g, 81);
    valid = true;
  }

  bool isGiven(int i) const { return given[i] != 0; }

  // Sets or clears (v = 0) a digit; clues cannot change. Returns true when something changed.
  bool setDigit(int i, int v) {
    if (i < 0 || i > 80 || given[i] || v < 0 || v > 9 || val[i] == v) return false;
    val[i] = static_cast<uint8_t>(v);
    if (v) notes[i] = 0;
    return true;
  }
  bool toggleNote(int i, int v) {
    if (i < 0 || i > 80 || given[i] || val[i] || v < 1 || v > 9) return false;
    notes[i] ^= static_cast<uint16_t>(1u << v);
    return true;
  }
  // Fills one cell with the right digit (counts as a hint). Returns false if nothing to do.
  bool hint(int i) {
    if (i < 0 || i > 80 || given[i] || val[i] == sol[i]) return false;
    val[i] = sol[i];
    notes[i] = 0;
    ++hints;
    return true;
  }

  // A digit that appears twice in a row, column or box.
  bool conflict(int i) const {
    const int v = val[i];
    if (!v) return false;
    const int r = i / 9, c = i % 9;
    for (int k = 0; k < 9; ++k) {
      if (k != c && val[r * 9 + k] == v) return true;
      if (k != r && val[k * 9 + c] == v) return true;
    }
    const int br = r / 3 * 3, bc = c / 3 * 3;
    for (int y = 0; y < 3; ++y)
      for (int x = 0; x < 3; ++x) {
        const int j = (br + y) * 9 + bc + x;
        if (j != i && val[j] == v) return true;
      }
    return false;
  }

  bool solved() const {
    for (int i = 0; i < 81; ++i)
      if (val[i] != sol[i]) return false;
    return true;
  }
  int filled() const {
    int n = 0;
    for (int i = 0; i < 81; ++i) n += val[i] != 0;
    return n;
  }
  int count(int digit) const {
    int n = 0;
    for (int i = 0; i < 81; ++i) n += val[i] == digit;
    return n;
  }

  // "S1 level hints given81 sol81 val81"
  std::string serialize() const {
    std::string s = "S1 ";
    s += static_cast<char>('0' + static_cast<int>(level));
    s += ' ';
    s += std::to_string(hints);
    s += ' ';
    for (int i = 0; i < 81; ++i) s += static_cast<char>('0' + given[i]);
    s += ' ';
    for (int i = 0; i < 81; ++i) s += static_cast<char>('0' + sol[i]);
    s += ' ';
    for (int i = 0; i < 81; ++i) s += static_cast<char>('0' + val[i]);
    s += '\n';
    return s;
  }

  bool parse(const std::string& text) {
    char lv = 0;
    int h = 0;
    char a[90] = {}, b[90] = {}, c[90] = {};
    if (sscanf(text.c_str(), "S1 %c %d %84s %84s %84s", &lv, &h, a, b, c) != 5) return false;
    if (lv < '0' || lv > '2' || strlen(a) != 81 || strlen(b) != 81 || strlen(c) != 81) return false;
    Sudoku g;
    for (int i = 0; i < 81; ++i) {
      if (a[i] < '0' || a[i] > '9' || b[i] < '1' || b[i] > '9' || c[i] < '0' || c[i] > '9') return false;
      g.given[i] = static_cast<uint8_t>(a[i] - '0');
      g.sol[i] = static_cast<uint8_t>(b[i] - '0');
      g.val[i] = static_cast<uint8_t>(c[i] - '0');
      if (g.given[i] && (g.given[i] != g.sol[i] || g.val[i] != g.given[i])) return false;
    }
    // The stored solution must really be a solved grid.
    for (int i = 0; i < 81; ++i) {
      for (int k = 0; k < 9; ++k) {
        const int r = i / 9, cc = i % 9;
        if (k != cc && g.sol[r * 9 + k] == g.sol[i]) return false;
        if (k != r && g.sol[k * 9 + cc] == g.sol[i]) return false;
      }
    }
    g.level = static_cast<SudokuLevel>(lv - '0');
    g.hints = h < 0 ? 0 : h;
    g.valid = true;
    *this = g;
    return true;
  }

  // Counts the solutions of a grid (stops at "limit"); node budget guards against slow cases.
  static int countSolutions(const uint8_t* grid, int limit, long& nodes) {
    uint8_t g[81];
    memcpy(g, grid, 81);
    int found = 0;
    search(g, limit, found, nodes);
    return found;
  }

 private:
  static uint16_t candidates(const uint8_t* g, int i) {
    uint16_t used = 0;
    const int r = i / 9, c = i % 9;
    for (int k = 0; k < 9; ++k) {
      used |= static_cast<uint16_t>(1u << g[r * 9 + k]);
      used |= static_cast<uint16_t>(1u << g[k * 9 + c]);
    }
    const int br = r / 3 * 3, bc = c / 3 * 3;
    for (int y = 0; y < 3; ++y)
      for (int x = 0; x < 3; ++x) used |= static_cast<uint16_t>(1u << g[(br + y) * 9 + bc + x]);
    return static_cast<uint16_t>(0x3FE & ~used);
  }
  static int popcount(uint16_t m) {
    int n = 0;
    while (m) {
      m &= static_cast<uint16_t>(m - 1);
      ++n;
    }
    return n;
  }

  // Backtracking with "fewest candidates first". Counts solutions up to limit.
  static void search(uint8_t* g, int limit, int& found, long& nodes) {
    if (found >= limit || nodes <= 0) return;
    --nodes;
    int best = -1, bestN = 10;
    uint16_t bestM = 0;
    for (int i = 0; i < 81; ++i) {
      if (g[i]) continue;
      const uint16_t m = candidates(g, i);
      const int n = popcount(m);
      if (n < bestN) {
        bestN = n;
        best = i;
        bestM = m;
        if (n <= 1) break;
      }
    }
    if (best < 0) {
      ++found;
      return;
    }
    for (int v = 1; v <= 9; ++v) {
      if (!(bestM & (1u << v))) continue;
      g[best] = static_cast<uint8_t>(v);
      search(g, limit, found, nodes);
      g[best] = 0;
      if (found >= limit || nodes <= 0) return;
    }
  }

  // Random complete grid: same search, but with the digits tried in random order.
  static bool fill(uint8_t* g, Rng& rng, long& budget) {
    if (budget-- <= 0) return false;
    int best = -1, bestN = 10;
    uint16_t bestM = 0;
    for (int i = 0; i < 81; ++i) {
      if (g[i]) continue;
      const uint16_t m = candidates(g, i);
      const int n = popcount(m);
      if (n < bestN) {
        bestN = n;
        best = i;
        bestM = m;
        if (n == 0) return false;
      }
    }
    if (best < 0) return true;
    int digits[9];
    int n = 0;
    for (int v = 1; v <= 9; ++v)
      if (bestM & (1u << v)) digits[n++] = v;
    for (int i = n - 1; i > 0; --i) {
      const int j = rng.below(i + 1);
      const int t = digits[i];
      digits[i] = digits[j];
      digits[j] = t;
    }
    for (int k = 0; k < n; ++k) {
      g[best] = static_cast<uint8_t>(digits[k]);
      if (fill(g, rng, budget)) return true;
      g[best] = 0;
    }
    return false;
  }
};

}  // namespace capps
