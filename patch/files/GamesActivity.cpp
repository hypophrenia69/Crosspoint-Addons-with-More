#include "GamesActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "AppsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

using Btn = MappedInputManager::Button;

void centered(const GfxRenderer& r, int font, int cx, int cy, const char* text, bool black = true) {
  const int w = r.getTextWidth(font, text);
  const int h = r.getLineHeight(font);
  r.drawText(font, cx - w / 2, cy - h / 2, text, black);
}

void rightAligned(const GfxRenderer& r, int font, int right, int y, const char* text) {
  r.drawText(font, right - r.getTextWidth(font, text), y, text);
}

void drawKey(const GfxRenderer& r, int x, int y, int w, int h, const char* label, bool filled, int font) {
  if (filled) {
    r.fillRect(x, y, w, h, true);
    centered(r, font, x + w / 2, y + h / 2, label, false);
  } else {
    r.drawRect(x, y, w, h, 2, true);
    centered(r, font, x + w / 2, y + h / 2, label, true);
  }
}

bool inside(int px, int py, int x, int y, int w, int h) { return px >= x && px < x + w && py >= y && py < y + h; }

uint32_t makeSeed(uint32_t& counter) { return (millis() * 2654435761u) ^ (counter++ * 40503u) ^ 0x9E3779B9u; }

constexpr const char* kStarterSudoku = "Sudoku. Open this file in the file browser to start it.\n";
constexpr const char* kStarterKnucklebones = "Knucklebones. Open this file in the file browser to start it.\n";

// One row of a menu (shared look with the Apps screen).
void drawMenuRow(const GfxRenderer& r, int x, int y, int w, int h, const char* label, const char* hint, bool focused) {
  if (focused) r.fillRect(x, y, w, h, true);
  else r.drawRect(x, y, w, h, 2, true);
  const int textH = r.getLineHeight(UI_12_FONT_ID);
  r.drawText(UI_12_FONT_ID, x + 20, y + h / 2 - textH / 2, label, !focused);
  if (hint && *hint) {
    const int hw = r.getTextWidth(UI_10_FONT_ID, hint);
    r.drawText(UI_10_FONT_ID, x + w - 20 - hw, y + h / 2 - r.getLineHeight(UI_10_FONT_ID) / 2, hint, !focused);
  }
}

}  // namespace

// ======================================================================== Sudoku

void SudokuActivity::loadSaved() {
  hasSaved = false;
  const String text = Storage.readFile(filePath.c_str());
  capps::Sudoku g;
  if (g.parse(std::string(text.c_str())) && !g.solved()) {
    game = g;
    hasSaved = true;
  }
}

void SudokuActivity::save() {
  if (!game.valid) return;
  if (game.solved()) {
    Storage.writeFile(filePath.c_str(), String(kStarterSudoku));
    hasSaved = false;
  } else {
    Storage.writeFile(filePath.c_str(), String(game.serialize().c_str()));
    hasSaved = true;
  }
}

void SudokuActivity::startNew(capps::SudokuLevel level) {
  game.generate(level, makeSeed(seedCounter));
  cursor = 40;
  notesMode = false;
  wasSolved = false;
  screen = Screen::Play;
  save();
}

void SudokuActivity::onEnter() {
  Activity::onEnter();
  loadSaved();
  screen = Screen::Menu;
  menuSelected = 0;
  requestUpdate();
}

void SudokuActivity::menuRow(int index, Rect& r) const {
  r.x = 16;
  r.w = renderer.getScreenWidth() - 32;
  r.h = 92;
  r.y = 130 + index * (r.h + 12);
}

void SudokuActivity::openMenuItem(int index) {
  RenderLock lock(*this);
  if (hasSaved && index == 0) {
    screen = Screen::Play;
    wasSolved = false;
    return;
  }
  const int level = index - (hasSaved ? 1 : 0);
  startNew(static_cast<capps::SudokuLevel>(level));
}

void SudokuActivity::gridRect(Rect& r, int& cell) const {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  cell = std::min((w - 32) / 9, (h - 348) / 9);
  r.w = r.h = cell * 9;
  r.x = (w - r.w) / 2;
  r.y = 92;
}

void SudokuActivity::padRect(int key, Rect& r) const {
  Rect g;
  int cell;
  gridRect(g, cell);
  const int w = renderer.getScreenWidth();
  const int gap = 6;
  r.w = (w - 32 - 4 * gap) / 5;
  r.h = 52;
  r.x = 16 + (key % 5) * (r.w + gap);
  r.y = g.y + g.h + 14 + (key / 5) * (r.h + gap);
}

void SudokuActivity::actionRect(int index, Rect& r) const {
  Rect last;
  padRect(9, last);
  const int w = renderer.getScreenWidth();
  const int gap = 8;
  r.w = (w - 32 - 2 * gap) / 3;
  r.h = 52;
  r.x = 16 + index * (r.w + gap);
  r.y = last.y + last.h + 4 + gap;
}

void SudokuActivity::afterChange() {
  if (game.solved() && !wasSolved) {
    wasSolved = true;
  }
  save();
}

void SudokuActivity::enterDigit(int digit) {
  RenderLock lock(*this);
  if (game.isGiven(cursor)) return;
  if (digit == 0) {
    if (game.val[cursor]) game.setDigit(cursor, 0);
    else game.notes[cursor] = 0;
  } else if (notesMode && game.val[cursor] == 0) {
    game.toggleNote(cursor, digit);
  } else {
    game.setDigit(cursor, digit == game.val[cursor] ? 0 : digit);
  }
  afterChange();
}

void SudokuActivity::loop() {
  using capps::SudokuLevel;
  if (mappedInput.wasReleased(Btn::Back)) {
    if (screen == Screen::Play) {
      RenderLock lock(*this);
      save();
      screen = Screen::Menu;
      menuSelected = 0;
      requestUpdate();
    } else {
      capps::openAppsMenu(renderer, mappedInput);
    }
    return;
  }

  int tx = 0;
  int ty = 0;
  const bool tapped = mappedInput.wasScreenTapped(tx, ty);

  if (screen == Screen::Menu) {
    const int count = menuCount();
    if (tapped) {
      for (int i = 0; i < count; ++i) {
        Rect r;
        menuRow(i, r);
        if (inside(tx, ty, r.x, r.y, r.w, r.h)) {
          openMenuItem(i);
          requestUpdate();
          return;
        }
      }
    } else if (mappedInput.wasPressed(Btn::Up) || mappedInput.wasPressed(Btn::Left)) {
      menuSelected = (menuSelected + count - 1) % count;
      requestUpdate();
    } else if (mappedInput.wasPressed(Btn::Down) || mappedInput.wasPressed(Btn::Right)) {
      menuSelected = (menuSelected + 1) % count;
      requestUpdate();
    } else if (mappedInput.wasPressed(Btn::Confirm)) {
      openMenuItem(menuSelected);
      requestUpdate();
    }
    return;
  }

  // ---- play
  bool changed = false;
  if (tapped) {
    Rect g;
    int cell;
    gridRect(g, cell);
    if (inside(tx, ty, g.x, g.y, g.w, g.h)) {
      RenderLock lock(*this);
      cursor = (ty - g.y) / cell * 9 + (tx - g.x) / cell;
      changed = true;
    } else {
      for (int k = 0; k < 10 && !changed; ++k) {
        Rect r;
        padRect(k, r);
        if (inside(tx, ty, r.x, r.y, r.w, r.h)) {
          enterDigit(k < 9 ? k + 1 : 0);
          changed = true;
        }
      }
      for (int a = 0; a < 3 && !changed; ++a) {
        Rect r;
        actionRect(a, r);
        if (!inside(tx, ty, r.x, r.y, r.w, r.h)) continue;
        RenderLock lock(*this);
        if (a == 0) {
          notesMode = !notesMode;
        } else if (a == 1) {
          if (game.hint(cursor)) afterChange();
        } else {
          save();
          screen = Screen::Menu;
          menuSelected = 0;
        }
        changed = true;
      }
    }
  } else if (mappedInput.wasPressed(Btn::Left)) {
    RenderLock lock(*this);
    cursor = (cursor % 9 == 0) ? cursor + 8 : cursor - 1;
    changed = true;
  } else if (mappedInput.wasPressed(Btn::Right)) {
    RenderLock lock(*this);
    cursor = (cursor % 9 == 8) ? cursor - 8 : cursor + 1;
    changed = true;
  } else if (mappedInput.wasPressed(Btn::Up)) {
    RenderLock lock(*this);
    cursor = (cursor + 72) % 81;
    changed = true;
  } else if (mappedInput.wasPressed(Btn::Down)) {
    RenderLock lock(*this);
    cursor = (cursor + 9) % 81;
    changed = true;
  } else if (mappedInput.wasPressed(Btn::Confirm)) {
    if (wasSolved) {
      RenderLock lock(*this);
      screen = Screen::Menu;
      menuSelected = 0;
    } else if (!game.isGiven(cursor)) {
      RenderLock lock(*this);
      game.setDigit(cursor, (game.val[cursor] + 1) % 10);
      afterChange();
    }
    changed = true;
  }
  if (changed) requestUpdate();
}

void SudokuActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const bool touch = mappedInput.hasTouch();

  if (screen == Screen::Menu) {
    renderer.drawText(UI_12_FONT_ID, 16, 40, "Sudoku");
    renderer.drawLine(16, 92, w - 16, 92, 2, true);
    static const char* const NAMES[3] = {"New game: Easy", "New game: Medium", "New game: Hard"};
    static const char* const HINTS[3] = {"40 clues", "32 clues", "26 clues"};
    int row = 0;
    if (hasSaved) {
      Rect r;
      menuRow(row, r);
      char hint[24];
      snprintf(hint, sizeof(hint), "%d / 81", game.filled());
      drawMenuRow(renderer, r.x, r.y, r.w, r.h, "Continue", hint, !touch && menuSelected == row);
      ++row;
    }
    for (int i = 0; i < 3; ++i, ++row) {
      Rect r;
      menuRow(row, r);
      drawMenuRow(renderer, r.x, r.y, r.w, r.h, NAMES[i], HINTS[i], !touch && menuSelected == row);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), touch ? "" : "OK", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  // ---- play screen
  static const char* const LEVELS[3] = {"Easy", "Medium", "Hard"};
  char title[48];
  snprintf(title, sizeof(title), "Sudoku  %s", LEVELS[static_cast<int>(game.level)]);
  renderer.drawText(UI_12_FONT_ID, 16, 20, title);
  char status[48];
  if (game.solved()) snprintf(status, sizeof(status), "Solved!  Hints used: %d", game.hints);
  else snprintf(status, sizeof(status), "%d / 81   Hints: %d", game.filled(), game.hints);
  rightAligned(renderer, UI_10_FONT_ID, w - 16, 54, status);

  Rect g;
  int cell;
  gridRect(g, cell);
  for (int i = 0; i < 81; ++i) {
    const int cx = g.x + (i % 9) * cell;
    const int cy = g.y + (i / 9) * cell;
    const bool sel = i == cursor;
    if (sel) renderer.fillRect(cx + 1, cy + 1, cell - 2, cell - 2, true);
    const int v = game.val[i];
    if (v) {
      char t[2] = {static_cast<char>('0' + v), 0};
      centered(renderer, UI_12_FONT_ID, cx + cell / 2, cy + cell / 2, t, !sel);
      if (game.isGiven(i)) centered(renderer, UI_12_FONT_ID, cx + cell / 2 + 1, cy + cell / 2, t, !sel);
      if (game.conflict(i)) renderer.drawRect(cx + 4, cy + 4, cell - 8, cell - 8, 2, !sel);
    } else if (game.notes[i]) {
      const int sub = cell / 3;
      for (int n = 1; n <= 9; ++n) {
        if (!(game.notes[i] & (1u << n))) continue;
        const int px = cx + ((n - 1) % 3) * sub + sub / 2 - 2;
        const int py = cy + ((n - 1) / 3) * sub + sub / 2 - 2;
        renderer.fillRect(px, py, 5, 5, !sel);
      }
    }
  }
  for (int k = 0; k <= 9; ++k) {
    const int lw = (k % 3 == 0) ? 3 : 1;
    renderer.drawLine(g.x, g.y + k * cell, g.x + g.w, g.y + k * cell, lw, true);
    renderer.drawLine(g.x + k * cell, g.y, g.x + k * cell, g.y + g.h, lw, true);
  }

  for (int k = 0; k < 10; ++k) {
    Rect r;
    padRect(k, r);
    char t[2] = {static_cast<char>('0' + k + 1), 0};
    drawKey(renderer, r.x, r.y, r.w, r.h, k < 9 ? t : "Del", false, UI_12_FONT_ID);
  }
  static const char* const ACTIONS[3] = {"Notes", "Hint", "Menu"};
  for (int a = 0; a < 3; ++a) {
    Rect r;
    actionRect(a, r);
    drawKey(renderer, r.x, r.y, r.w, r.h, ACTIONS[a], a == 0 && notesMode, UI_10_FONT_ID);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), touch ? "" : "+1", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (++updatesSinceFull >= 10) {
    updatesSinceFull = 0;
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

// =================================================================== Knucklebones

void KnucklebonesActivity::loadSaved() {
  hasSaved = false;
  const String text = Storage.readFile(filePath.c_str());
  capps::Knucklebones g;
  if (g.parse(std::string(text.c_str()))) {
    game = g;
    hasSaved = !g.over;
  }
}

void KnucklebonesActivity::save() { Storage.writeFile(filePath.c_str(), String(game.serialize().c_str())); }

void KnucklebonesActivity::startGame(bool cpu, int difficulty) {
  game.newGame(cpu, difficulty, makeSeed(seedCounter));
  last = capps::KBMove();
  message = cpu ? "Your turn: tap a column" : "Player 1: tap a column";
  cpuDueMs = 0;
  column = 1;
  moves = 0;
  screen = Screen::Play;
  save();
}

void KnucklebonesActivity::onEnter() {
  Activity::onEnter();
  loadSaved();
  screen = Screen::Menu;
  menuSelected = 0;
  requestUpdate();
}

void KnucklebonesActivity::menuRow(int index, Rect& r) const {
  r.x = 16;
  r.w = renderer.getScreenWidth() - 32;
  r.h = 84;
  r.y = 140 + index * (r.h + 10);
}

void KnucklebonesActivity::openMenuItem(int index) {
  RenderLock lock(*this);
  if (hasSaved) {
    if (index == 0) {
      screen = Screen::Play;
      message = game.vsCpu ? "Your turn: tap a column" : "Tap a column";
      if (game.cpuToMove()) cpuDueMs = (millis() + 500) | 1u;
      return;
    }
    --index;
  }
  if (index <= 2) startGame(true, index);
  else if (index == 3) startGame(false, 1);
  else screen = Screen::Rules;
}

int KnucklebonesActivity::cellSize() const { return std::min(72, (renderer.getScreenWidth() - 64) / 3); }

void KnucklebonesActivity::boardOrigin(int player, int& x, int& y) const {
  const int cs = cellSize();
  const int bw = 3 * cs + 12;
  x = (renderer.getScreenWidth() - bw) / 2;
  y = player == 2 ? 92 : 92 + bw + 120;
}

void KnucklebonesActivity::describe(const capps::KBMove& m, bool cpu) {
  char buf[80];
  const char* who = cpu ? "CPU" : (game.vsCpu ? "You" : (m.player == 1 ? "Player 1" : "Player 2"));
  if (m.smashed > 0) snprintf(buf, sizeof(buf), "%s: %d in column %d, smashed %d", who, m.die, m.col + 1, m.smashed);
  else snprintf(buf, sizeof(buf), "%s: %d in column %d", who, m.die, m.col + 1);
  message = buf;
}

void KnucklebonesActivity::playColumn(int c) {
  RenderLock lock(*this);
  capps::KBMove m;
  if (!game.play(c, m)) {
    message = "Column full";
    return;
  }
  last = m;
  describe(m, false);
  ++moves;
  if (game.cpuToMove()) cpuDueMs = (millis() + 900) | 1u;
  save();
}

void KnucklebonesActivity::showFrame() {
  if (++updatesSinceFull >= 6) {
    updatesSinceFull = 0;
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

void KnucklebonesActivity::loop() {
  if (mappedInput.wasReleased(Btn::Back)) {
    if (screen == Screen::Menu) {
      capps::openAppsMenu(renderer, mappedInput);
    } else {
      RenderLock lock(*this);
      if (screen == Screen::Play) save();
      cpuDueMs = 0;
      screen = Screen::Menu;
      menuSelected = 0;
      loadSaved();
      requestUpdate();
    }
    return;
  }

  int tx = 0;
  int ty = 0;
  const bool tapped = mappedInput.wasScreenTapped(tx, ty);

  if (screen == Screen::Rules) {
    if (tapped || mappedInput.wasPressed(Btn::Confirm)) {
      screen = Screen::Menu;
      requestUpdate();
    }
    return;
  }

  if (screen == Screen::Menu) {
    const int count = menuCount();
    if (tapped) {
      for (int i = 0; i < count; ++i) {
        Rect r;
        menuRow(i, r);
        if (inside(tx, ty, r.x, r.y, r.w, r.h)) {
          openMenuItem(i);
          requestUpdate();
          return;
        }
      }
    } else if (mappedInput.wasPressed(Btn::Up) || mappedInput.wasPressed(Btn::Left)) {
      menuSelected = (menuSelected + count - 1) % count;
      requestUpdate();
    } else if (mappedInput.wasPressed(Btn::Down) || mappedInput.wasPressed(Btn::Right)) {
      menuSelected = (menuSelected + 1) % count;
      requestUpdate();
    } else if (mappedInput.wasPressed(Btn::Confirm)) {
      openMenuItem(menuSelected);
      requestUpdate();
    }
    return;
  }

  // ---- play: the computer answers after a short pause
  if (cpuDueMs != 0) {
    if (static_cast<int32_t>(millis() - cpuDueMs) >= 0) {
      RenderLock lock(*this);
      cpuDueMs = 0;
      capps::KBMove m;
      if (game.playCpu(m)) {
        last = m;
        describe(m, true);
        ++moves;
        save();
      }
      requestUpdate();
    }
    return;
  }

  if (game.over) {
    if (tapped || mappedInput.wasPressed(Btn::Confirm)) {
      RenderLock lock(*this);
      startGame(game.vsCpu, game.difficulty);
      requestUpdate();
    }
    return;
  }

  const int cs = cellSize();
  int bx, by;
  boardOrigin(game.turn, bx, by);
  if (tapped) {
    for (int c = 0; c < 3; ++c) {
      const int x = bx + c * (cs + 6);
      if (inside(tx, ty, x - 6, by - 6, cs + 12, 3 * cs + 12 + 12)) {
        playColumn(c);
        requestUpdate();
        return;
      }
    }
  } else if (mappedInput.wasPressed(Btn::Left) || mappedInput.wasPressed(Btn::Up)) {
    RenderLock lock(*this);
    column = (column + 2) % 3;
    requestUpdate();
  } else if (mappedInput.wasPressed(Btn::Right) || mappedInput.wasPressed(Btn::Down)) {
    RenderLock lock(*this);
    column = (column + 1) % 3;
    requestUpdate();
  } else if (mappedInput.wasPressed(Btn::Confirm)) {
    playColumn(column);
    requestUpdate();
  }
}

namespace {

void drawPips(const GfxRenderer& r, int x, int y, int size, int v) {
  const int p = std::max(6, size / 8);
  const int a = size / 4, b = size / 2, c = size * 3 / 4;
  const auto pip = [&](int px, int py) { r.fillRect(x + px - p / 2, y + py - p / 2, p, p, true); };
  if (v == 1 || v == 3 || v == 5) pip(b, b);
  if (v >= 2) { pip(a, a); pip(c, c); }
  if (v >= 4) { pip(c, a); pip(a, c); }
  if (v == 6) { pip(a, b); pip(c, b); }
}

}  // namespace

void KnucklebonesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const bool touch = mappedInput.hasTouch();

  if (screen == Screen::Menu) {
    renderer.drawText(UI_12_FONT_ID, 16, 40, "Knucklebones");
    if (game.wins1 + game.wins2 + game.draws > 0) {
      char rec[64];
      snprintf(rec, sizeof(rec), "Record vs CPU / 2P:  wins %d   losses %d   draws %d", game.wins1, game.wins2, game.draws);
      renderer.drawText(UI_10_FONT_ID, 16, 82, rec);
    }
    renderer.drawLine(16, 120, w - 16, 120, 2, true);
    static const char* const LABELS[5] = {"vs computer: Easy", "vs computer: Medium", "vs computer: Hard",
                                          "Two players", "Rules"};
    static const char* const HINTS[5] = {"random", "greedy", "plans ahead", "one device", ""};
    int row = 0;
    if (hasSaved) {
      Rect r;
      menuRow(row, r);
      drawMenuRow(renderer, r.x, r.y, r.w, r.h, "Continue", game.vsCpu ? "vs computer" : "two players",
                  !touch && menuSelected == row);
      ++row;
    }
    for (int i = 0; i < 5; ++i, ++row) {
      Rect r;
      menuRow(row, r);
      drawMenuRow(renderer, r.x, r.y, r.w, r.h, LABELS[i], HINTS[i], !touch && menuSelected == row);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), touch ? "" : "OK", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  if (screen == Screen::Rules) {
    renderer.drawText(UI_12_FONT_ID, 16, 40, "Rules");
    renderer.drawLine(16, 92, w - 16, 92, 2, true);
    static const char* const LINES[] = {
        "Two boards with three columns each.",
        "Roll a die and drop it into a column",
        "of your own board (three dice per column).",
        "",
        "Equal dice in a column multiply:",
        "two of a kind count double each,",
        "three of a kind count triple each.",
        "",
        "Your die destroys all dice of the same",
        "value in the opposite column of the",
        "other board.",
        "",
        "The game ends when one board is full.",
        "The higher total wins.",
        "",
        "Touch: tap a column.",
        "Keys: left / right, OK places.",
    };
    int y = 112;
    for (const char* line : LINES) {
      renderer.drawText(UI_10_FONT_ID, 16, y, line);
      y += 32;
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  // ---- play screen
  const int cs = cellSize();
  const int bw = 3 * cs + 12;
  for (int player = 2; player >= 1; --player) {
    const capps::KBoard& b = player == 1 ? game.b1 : game.b2;
    int bx, by;
    boardOrigin(player, bx, by);
    const bool active = !game.over && game.turn == player;
    if (active) renderer.drawRect(bx - 8, by - 8, bw + 16, bw + 16, 3, true);
    for (int c = 0; c < 3; ++c) {
      const int x = bx + c * (cs + 6);
      for (int s = 0; s < 3; ++s) {
        // Slot 0 is the die nearest the middle of the table.
        const int row = player == 2 ? 2 - s : s;
        const int y = by + row * (cs + 3);
        renderer.drawRect(x, y, cs, cs, 2, true);
        if (b.d[c][s]) drawPips(renderer, x, y, cs, b.d[c][s]);
        if (last.col == c && last.slot == s && last.player == player) renderer.drawRect(x + 4, y + 4, cs - 8, cs - 8, 3, true);
      }
      char sc[8];
      snprintf(sc, sizeof(sc), "%d", b.colScore(c));
      centered(renderer, UI_10_FONT_ID, x + cs / 2, player == 2 ? by - 26 : by + bw + 14, sc);
    }
    if (!touch && active) {
      const int x = bx + column * (cs + 6);
      renderer.drawRect(x - 4, by - 4, cs + 8, bw + 8, 4, true);
    }
    char name[32];
    if (player == 1) snprintf(name, sizeof(name), "%s", game.vsCpu ? "You" : "Player 1");
    else snprintf(name, sizeof(name), "%s", game.vsCpu ? "Computer" : "Player 2");
    char total[24];
    snprintf(total, sizeof(total), "Total %d", b.score());
    const int ly = player == 2 ? 20 : by + bw + 34;
    renderer.drawText(UI_12_FONT_ID, 16, ly, name);
    rightAligned(renderer, UI_12_FONT_ID, w - 16, ly, total);
  }

  // Middle strip: the waiting die or the result.
  int bx, by;
  boardOrigin(2, bx, by);
  const int midTop = by + bw + 8;
  if (game.over) {
    const int win = game.winner();
    const char* res = win == 0 ? "Draw" : (game.vsCpu ? (win == 1 ? "You win!" : "Computer wins") : (win == 1 ? "Player 1 wins!" : "Player 2 wins!"));
    centered(renderer, UI_12_FONT_ID, w / 2, midTop + 36, res);
    centered(renderer, UI_10_FONT_ID, w / 2, midTop + 76, touch ? "Tap for a new game" : "OK: new game");
  } else {
    const int size = 64;
    const int dx = w / 2 - size / 2;
    const int dy = midTop + 6;
    renderer.drawRect(dx, dy, size, size, 3, true);
    if (cpuDueMs == 0) drawPips(renderer, dx, dy, size, game.die);
    const char* who = game.vsCpu ? (game.turn == 1 ? "Your die" : "CPU rolls") : (game.turn == 1 ? "Player 1" : "Player 2");
    renderer.drawText(UI_10_FONT_ID, 16, dy + size / 2 - renderer.getLineHeight(UI_10_FONT_ID) / 2, who);
    centered(renderer, UI_10_FONT_ID, w / 2, dy + size + 18, message.c_str());
  }
  if (game.over && !message.empty()) {
    centered(renderer, UI_10_FONT_ID, w / 2, midTop + 108, message.c_str());
  }

  const auto labels = mappedInput.mapLabels("Menu", touch || game.over ? "" : "Place", touch ? "" : "<", touch ? "" : ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  showFrame();
}
