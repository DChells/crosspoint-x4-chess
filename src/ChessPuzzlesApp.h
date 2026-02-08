#pragma once

#include <HalDisplay.h>
#include <HalGPIO.h>
#include <GfxRenderer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <esp_partition.h>

#include <vector>
#include <string>


#include "ChessCore.h"

class ChessPuzzlesApp final {
 public:
  ChessPuzzlesApp(HalDisplay& display, HalGPIO& input);

  void onEnter();
  void onExit();
  void loop();

 private:
  HalDisplay& display_;
  HalGPIO& input_;
  GfxRenderer renderer_;

  enum class Mode { PackSelect, PackMenu, ThemeSelect, Browsing, Playing, InGameMenu };
  Mode currentMode = Mode::PackSelect;
  
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  int movesSinceFullRefresh = 0;
  bool pendingFullRefresh = false;

  Chess::BoardState board;
  bool playerIsWhite = true;
  
  Chess::Puzzle currentPuzzle;
  int currentMoveIndex = 0;
  bool puzzleSolved = false;
  bool puzzleFailed = false;
  bool hintActive = false;
  bool ignoreBackRelease = false;
  
  int cursorFile = 4;
  int cursorRank = 3;
  
  bool pieceSelected = false;
  int selectedSquare = -1;
  std::vector<Chess::Move> legalMovesFromSelected;

  std::vector<int> navigablePieces;
  int navigablePieceIndex = 0;
  int legalMoveNavIndex = 0;
  
  static constexpr int SQUARE_SIZE = 60;
  static constexpr int BOARD_SIZE = SQUARE_SIZE * 8;
  static constexpr int BOARD_OFFSET_X = 0;
  static constexpr int BOARD_OFFSET_Y = 0;
  static constexpr int STATUS_Y = BOARD_SIZE + 10;\n  static constexpr int MENU_HIGHLIGHT_PADDING = 8;\n  
  std::string packPath;
  std::string packName;
  uint32_t puzzleCount = 0;
  uint16_t packRecordSize = Chess::RECORD_SIZE;
  uint32_t currentPuzzleIndex = 0;
  uint32_t solvedCount = 0;
  
  std::vector<std::string> availablePacks;
  int packSelectorIndex = 0;
  
  enum class PackMenuItem { Continue, Random, Themes, Browse };
  int packMenuIndex = 0;
  static constexpr int PACK_MENU_ITEM_COUNT = 4;
  
  uint32_t browserIndex = 0;

  enum class InGameMenuItem { Retry, Skip, Hint, RefreshScreen, Exit };
  int inGameMenuIndex = 0;
  static constexpr int IN_GAME_MENU_ITEM_COUNT = 5;
  static constexpr unsigned long IN_GAME_MENU_HOLD_MS = 800;
  
  std::vector<uint8_t> solvedBitset;
  
  std::vector<std::string> availableThemes;
  int themeSelectIndex = 0;
  std::string activeTheme;
  std::vector<uint8_t> themeBitset;
  
  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  
  void render();
  void renderPackSelect();
  void renderPackMenu();
  void renderThemeSelect();
  void renderBrowser();
  void renderInGameMenu();
  void renderBoard();
  void renderPiece(int file, int rank);
  void renderCursor();
  void renderLegalMoveHints();
  void renderHint();
  void renderStatus();
  
  void loadAvailablePacks();
  bool loadPackInfo();
  bool loadPuzzleFromPack(uint32_t index);
  void loadNextPuzzle();
  void loadRandomPuzzle();
  void loadDemoPuzzle();
  
  void saveProgress();
  uint32_t loadProgress();
  std::string getProgressPath() const;
  
  void loadSolvedBitset();
  void saveSolvedBitset();
  void markPuzzleSolved(uint32_t index);
  bool isPuzzleSolved(uint32_t index) const;
  void countSolvedPuzzles();
  std::string getSolvedPath() const;
  
  void loadAvailableThemes();
  void loadThemeBitset(const std::string& theme);
  bool puzzleMatchesTheme(uint32_t index) const;
  void loadRandomThemedPuzzle();
  
  void selectSquare(int sq);
  void deselectPiece();
  bool tryMove(const Chess::Move& move);
  void handlePlayerMove(const Chess::Move& move);
  void playOpponentMove();
  void onPuzzleSolved();
  void onPuzzleFailed();

  void triggerFullRefresh();
  void buildNavigablePieceList();

  void renderSdCardError();
  void renderPartitionError();
  void returnToLauncher();

  bool validatePartition(const esp_partition_t* partition);

  void logModeChange(Mode from, Mode to, const char* reason);
  void logEvent(const char* ev, const char* fmt = nullptr, ...) const;
  
  int cursorSquare() const { return cursorRank * 8 + cursorFile; }
  int screenX(int file) const;
  int screenY(int rank) const;
  bool isLegalDestination(int sq) const;
};
