#include <cassert>
#include <chrono>
#include <cstdio>
#include "../patch/files/GamesCore.h"
using namespace capps;

static bool gridValid(const uint8_t* g) {
  for (int i = 0; i < 81; ++i) {
    if (g[i] < 1 || g[i] > 9) return false;
    for (int k = 0; k < 9; ++k) {
      const int r = i / 9, c = i % 9;
      if (k != c && g[r * 9 + k] == g[i]) return false;
      if (k != r && g[k * 9 + c] == g[i]) return false;
    }
    const int br = i / 9 / 3 * 3, bc = i % 9 / 3 * 3;
    for (int y = 0; y < 3; ++y)
      for (int x = 0; x < 3; ++x) {
        const int j = (br + y) * 9 + bc + x;
        if (j != i && g[j] == g[i]) return false;
      }
  }
  return true;
}

int main() {
  // ---- Knucklebones rules
  KBoard b;
  assert(b.place(0, 4) == 0 && b.place(0, 4) == 1 && b.place(0, 2) == 2);
  assert(b.place(0, 1) == -1);               // column full
  assert(b.colScore(0) == 4 * 2 + 4 * 2 + 2);  // two 4s count double each
  KBoard t;
  t.place(1, 3); t.place(1, 3); t.place(1, 3);
  assert(t.colScore(1) == 27);               // three of a kind
  assert(t.smash(1, 3) == 3 && t.count(1) == 0);
  KBoard p;
  p.place(0, 5); p.place(0, 2); p.place(0, 5);
  assert(p.smash(0, 5) == 2 && p.d[0][0] == 2 && p.d[0][1] == 0);  // packs toward the centre

  Knucklebones g;
  g.newGame(false, 0, 123);
  assert(g.turn == 1 && g.die >= 1 && g.die <= 6);
  KBMove mv;
  g.die = 4;
  assert(g.play(0, mv) && mv.slot == 0 && g.turn == 2);
  g.die = 4;  // player 2 puts a 4 in column 0: smashes the 4 of player 1 in column 0
  assert(g.play(0, mv) && mv.smashed == 1 && g.b1.count(0) == 0);
  assert(!g.play(5, mv));

  // full games against every difficulty always finish and never place illegally
  for (int diff = 0; diff < 3; ++diff) {
    for (int game = 0; game < 40; ++game) {
      Knucklebones k;
      k.newGame(true, diff, 1000 + game * 7 + diff);
      int guard = 0;
      while (!k.over && guard++ < 200) {
        if (k.cpuToMove()) {
          assert(k.playCpu(mv));
        } else {
          int c = -1;
          for (int i = 0; i < 3; ++i) if (k.b1.hasRoom(i)) { c = i; break; }
          assert(k.play(c, mv));
        }
      }
      assert(k.over && (k.b1.full() || k.b2.full()));
      assert(k.wins1 + k.wins2 + k.draws == 1);
    }
  }
  // the hard computer beats the easy one clearly
  {
    int hardWins = 0, easyWins = 0;
    for (int game = 0; game < 200; ++game) {
      Knucklebones k;
      k.newGame(false, 0, 5000 + game);
      Rng r; r.seed(77 + game);
      int guard = 0;
      while (!k.over && guard++ < 200) {
        KBoard& mine = k.turn == 1 ? k.b1 : k.b2;
        KBoard& theirs = k.turn == 1 ? k.b2 : k.b1;
        const int diff = k.turn == 1 ? 2 : 0;
        const int c = kbChoose(diff, mine, theirs, k.die, r);
        assert(k.play(c, mv));
      }
      const int w = k.winner();
      hardWins += w == 1;
      easyWins += w == 2;
    }
    printf("hard vs easy: %d : %d\n", hardWins, easyWins);
    assert(hardWins > easyWins * 2);
  }
  // save and load
  {
    Knucklebones k;
    k.newGame(true, 2, 9);
    k.play(1, mv); k.playCpu(mv);
    Knucklebones l;
    assert(l.parse(k.serialize()));
    assert(l.serialize() == k.serialize());
    assert(!l.parse("K1 1 9 1 1 0 0 0 0 000000000000000000"));
    assert(!l.parse("garbage"));
    assert(!l.parse("K1 1 1 1 1 0 0 0 0 010000000000000000"));  // not packed
  }

  // ---- Sudoku
  for (int lv = 0; lv < 3; ++lv) {
    for (int n = 0; n < 15; ++n) {
      Sudoku s;
      const auto t0 = std::chrono::steady_clock::now();
      s.generate(static_cast<SudokuLevel>(lv), 4242u + n * 31 + lv);
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      assert(gridValid(s.sol));
      int clues = 0;
      for (int i = 0; i < 81; ++i) {
        if (s.given[i]) { ++clues; assert(s.given[i] == s.sol[i]); }
      }
      long nodes = 1000000;
      assert(Sudoku::countSolutions(s.given, 2, nodes) == 1);   // unique
      if (lv == 0) assert(clues <= 41);
      if (n == 0) printf("level %d: %d clues, %.1f ms\n", lv, clues, ms);
      assert(!s.solved() && s.filled() == clues);
    }
  }
  {
    Sudoku s;
    s.generate(SudokuLevel::Medium, 1);
    int free = -1;
    for (int i = 0; i < 81; ++i) if (!s.given[i]) { free = i; break; }
    int clue = -1;
    for (int i = 0; i < 81; ++i) if (s.given[i]) { clue = i; break; }
    assert(!s.setDigit(clue, 0));                 // clues are fixed
    const int wrong = s.sol[free] % 9 + 1;
    assert(s.setDigit(free, wrong));
    assert(!s.solved());
    assert(s.setDigit(free, 0) && s.val[free] == 0);
    assert(s.toggleNote(free, 3) && (s.notes[free] & (1 << 3)));
    assert(s.toggleNote(free, 3) && !(s.notes[free] & (1 << 3)));
    assert(s.hint(free) && s.val[free] == s.sol[free] && s.hints == 1);
    // conflict flag: put the same digit twice in a row
    Sudoku c;
    c.generate(SudokuLevel::Easy, 3);
    int a = -1, b = -1;
    for (int i = 0; i < 9 && b < 0; ++i) if (!c.given[i]) { if (a < 0) a = i; else b = i; }
    if (a >= 0 && b >= 0) {
      c.setDigit(a, 5); c.setDigit(b, 5);
      assert(c.conflict(a) && c.conflict(b));
    }
    // solve it completely
    for (int i = 0; i < 81; ++i) c.val[i] = c.sol[i];
    assert(c.solved());
    // save and load
    Sudoku d;
    assert(d.parse(c.serialize()) && d.serialize() == c.serialize() && d.solved());
    assert(!d.parse("S1 0 0 abc"));
    std::string bad = c.serialize();
    bad[20] = bad[20] == '1' ? '2' : '1';
    Sudoku e;
    (void)e.parse(bad);  // must not crash
  }
  printf("games OK\nALL OK\n");
  return 0;
}
