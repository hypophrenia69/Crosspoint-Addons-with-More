#pragma once
// Sudoku and Knucklebones screens (added by the Crosspoint-Apps patch).
// They open from "/Apps/Sudoku.txt" and "/Apps/Knucklebones.txt" in the file browser; the same
// file keeps the saved game.
//
//  X4 Pro (touch): tap the cells, keys and buttons.
//  X3 / X4 (keys): arrows move the frame, Confirm acts. Back leaves.

#include <string>
#include <vector>

#include "GamesCore.h"
#include "activities/Activity.h"

class SudokuActivity final : public Activity {
  enum class Screen : uint8_t { Menu, Play };
  struct Rect {
    int x, y, w, h;
  };

  std::string filePath;
  capps::Sudoku game;
  Screen screen = Screen::Menu;
  bool hasSaved = false;     // a saved, unfinished game exists
  int menuSelected = 0;
  int cursor = 40;           // selected cell (0..80)
  bool notesMode = false;
  bool wasSolved = false;
  int updatesSinceFull = 0;
  uint32_t seedCounter = 1;

  void loadSaved();
  void save();
  void startNew(capps::SudokuLevel level);
  void menuRow(int index, Rect& r) const;
  int menuCount() const { return hasSaved ? 4 : 3; }
  void openMenuItem(int index);
  void gridRect(Rect& r, int& cell) const;
  void padRect(int key, Rect& r) const;   // key 0..9: digits 1..9 then erase
  void actionRect(int index, Rect& r) const;  // 0 notes, 1 hint, 2 menu
  void enterDigit(int digit);
  void afterChange();

 public:
  SudokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
      : Activity("Sudoku", renderer, mappedInput), filePath(std::move(path)) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};

class KnucklebonesActivity final : public Activity {
  enum class Screen : uint8_t { Menu, Rules, Play };
  struct Rect {
    int x, y, w, h;
  };

  std::string filePath;
  capps::Knucklebones game;
  Screen screen = Screen::Menu;
  bool hasSaved = false;
  int menuSelected = 0;
  int column = 1;              // column frame for button devices
  uint32_t cpuDueMs = 0;       // when the computer moves (0 = not waiting)
  capps::KBMove last;          // the last move, outlined on the board
  std::string message;
  int updatesSinceFull = 0;
  int moves = 0;
  uint32_t seedCounter = 1;

  void loadSaved();
  void save();
  void startGame(bool cpu, int difficulty);
  void menuRow(int index, Rect& r) const;
  int menuCount() const { return hasSaved ? 6 : 5; }
  void openMenuItem(int index);
  int cellSize() const;
  void boardOrigin(int player, int& x, int& y) const;
  void playColumn(int c);
  void describe(const capps::KBMove& m, bool cpu);
  void showFrame();

 public:
  KnucklebonesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
      : Activity("Knucklebones", renderer, mappedInput), filePath(std::move(path)) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return cpuDueMs != 0; }
};
