#include "ChessPuzzlesApp.h"

#include <Arduino.h>
#include <SDCardManager.h>

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <builtinFonts/all.h>

#include <esp_ota_ops.h>
#include <esp_system.h>

#include <algorithm>

#include "ChessSprites.h"
#include "fontIds.h"

namespace {
EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);
}

void ChessPuzzlesApp::taskTrampoline(void* param) {
  auto* self = static_cast<ChessPuzzlesApp*>(param);
  self->displayTaskLoop();
}

ChessPuzzlesApp::ChessPuzzlesApp(HalDisplay& display, HalGPIO& input)
    : display_(display), input_(input), renderer_(display) {}

void ChessPuzzlesApp::onEnter() {
  display_.begin();
  renderer_.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer_.insertFont(UI_12_FONT_ID, ui12FontFamily);

  if (!SdMan.begin()) {
    renderSdCardError();
    return;
  }

  if (!ChessSprites::loadSprites()) {
    Serial.println("[CHESS] Failed to load sprites from SD card");
  }

  renderingMutex = xSemaphoreCreateMutex();

  currentMode = Mode::PackSelect;
  loadAvailablePacks();
  packSelectorIndex = 0;

  updateRequired = true;

  xTaskCreate(&ChessPuzzlesApp::taskTrampoline, "ChessPuzzlesTask",
              4096, this, 1, &displayTaskHandle);
}

void ChessPuzzlesApp::onExit() {
  if (renderingMutex) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
  }
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  if (renderingMutex) {
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }

  ChessSprites::freeSprites();
}

void ChessPuzzlesApp::loop() {
  if (currentMode == Mode::PackSelect) {
    if (input_.wasPressed(HalGPIO::BTN_UP)) {
      if (packSelectorIndex > 0) {
        packSelectorIndex--;
        updateRequired = true;
      }
    } else if (input_.wasPressed(HalGPIO::BTN_DOWN)) {
      if (packSelectorIndex < static_cast<int>(availablePacks.size()) - 1) {
        packSelectorIndex++;
        updateRequired = true;
      }
    } else if (input_.wasReleased(HalGPIO::BTN_CONFIRM)) {
      if (!availablePacks.empty()) {
        packPath = "/.crosspoint/chess/packs/" + availablePacks[packSelectorIndex];
        packName = availablePacks[packSelectorIndex];
        if (packName.size() > 4) {
          packName = packName.substr(0, packName.size() - 4);
        }
        if (loadPackInfo()) {
          loadSolvedBitset();
          countSolvedPuzzles();
          packMenuIndex = 0;
          currentMode = Mode::PackMenu;
        } else {
          loadDemoPuzzle();
          currentMode = Mode::Playing;
        }
        updateRequired = true;
      }
    } else if (input_.wasReleased(HalGPIO::BTN_BACK)) {
      returnToLauncher();
    }
    return;
  }

  if (currentMode == Mode::PackMenu) {
    if (input_.wasPressed(HalGPIO::BTN_UP)) {
      if (packMenuIndex > 0) {
        packMenuIndex--;
        updateRequired = true;
      }
    } else if (input_.wasPressed(HalGPIO::BTN_DOWN)) {
      if (packMenuIndex < PACK_MENU_ITEM_COUNT - 1) {
        packMenuIndex++;
        updateRequired = true;
      }
    } else if (input_.wasReleased(HalGPIO::BTN_CONFIRM)) {
      switch (static_cast<PackMenuItem>(packMenuIndex)) {
        case PackMenuItem::Continue: {
          uint32_t savedIndex = loadProgress();
          if (savedIndex >= puzzleCount) savedIndex = 0;
          activeTheme.clear();
          themeBitset.clear();
          if (loadPuzzleFromPack(savedIndex)) {
            currentMode = Mode::Playing;
          }
          break;
        }
        case PackMenuItem::Random:
          activeTheme.clear();
          themeBitset.clear();
          loadRandomPuzzle();
          currentMode = Mode::Playing;
          break;
        case PackMenuItem::Themes:
          loadAvailableThemes();
          themeSelectIndex = 0;
          currentMode = Mode::ThemeSelect;
          break;
        case PackMenuItem::Browse: {
          browserIndex = loadProgress();
          if (browserIndex >= puzzleCount) browserIndex = 0;
          activeTheme.clear();
          themeBitset.clear();
          currentMode = Mode::Browsing;
          break;
        }
      }
      updateRequired = true;
    } else if (input_.wasReleased(HalGPIO::BTN_BACK)) {
      currentMode = Mode::PackSelect;
      updateRequired = true;
    }
    return;
  }

  if (currentMode == Mode::ThemeSelect) {
    if (input_.wasPressed(HalGPIO::BTN_UP)) {
      if (themeSelectIndex > 0) {
        themeSelectIndex--;
        updateRequired = true;
      }
    } else if (input_.wasPressed(HalGPIO::BTN_DOWN)) {
      if (themeSelectIndex < static_cast<int>(availableThemes.size()) - 1) {
        themeSelectIndex++;
        updateRequired = true;
      }
    } else if (input_.wasReleased(HalGPIO::BTN_CONFIRM)) {
      if (!availableThemes.empty()) {
        activeTheme = availableThemes[themeSelectIndex];
        loadThemeBitset(activeTheme);
        loadRandomThemedPuzzle();
        currentMode = Mode::Playing;
        updateRequired = true;
      }
    } else if (input_.wasReleased(HalGPIO::BTN_BACK)) {
      currentMode = Mode::PackMenu;
      updateRequired = true;
    }
    return;
  }

  if (currentMode == Mode::Browsing) {
    if (input_.wasPressed(HalGPIO::BTN_UP)) {
      if (browserIndex > 0) {
        browserIndex--;
        updateRequired = true;
      }
    } else if (input_.wasPressed(HalGPIO::BTN_DOWN)) {
      if (browserIndex < puzzleCount - 1) {
        browserIndex++;
        updateRequired = true;
      }
    } else if (input_.wasPressed(HalGPIO::BTN_LEFT)) {
      if (browserIndex >= 10) {
        browserIndex -= 10;
      } else {
        browserIndex = 0;
      }
      updateRequired = true;
    } else if (input_.wasPressed(HalGPIO::BTN_RIGHT)) {
      browserIndex += 10;
      if (browserIndex >= puzzleCount) {
        browserIndex = puzzleCount - 1;
      }
      updateRequired = true;
    } else if (input_.wasReleased(HalGPIO::BTN_CONFIRM)) {
      if (loadPuzzleFromPack(browserIndex)) {
        currentMode = Mode::Playing;
        updateRequired = true;
      }
    } else if (input_.wasReleased(HalGPIO::BTN_BACK)) {
      currentMode = Mode::PackMenu;
      updateRequired = true;
    }
    return;
  }

  if (currentMode == Mode::InGameMenu) {
    if (input_.wasPressed(HalGPIO::BTN_UP)) {
      if (inGameMenuIndex > 0) {
        inGameMenuIndex--;
        updateRequired = true;
      }
    } else if (input_.wasPressed(HalGPIO::BTN_DOWN)) {
      if (inGameMenuIndex < IN_GAME_MENU_ITEM_COUNT - 1) {
        inGameMenuIndex++;
        updateRequired = true;
      }
    } else if (input_.wasReleased(HalGPIO::BTN_CONFIRM)) {
      switch (static_cast<InGameMenuItem>(inGameMenuIndex)) {
        case InGameMenuItem::Retry:
          loadPuzzleFromPack(currentPuzzleIndex);
          currentMode = Mode::Playing;
          break;
        case InGameMenuItem::Skip:
          if (!activeTheme.empty() && !themeBitset.empty()) {
            loadRandomThemedPuzzle();
          } else {
            loadNextPuzzle();
          }
          currentMode = Mode::Playing;
          break;
        case InGameMenuItem::Hint:
          hintActive = true;
          currentMode = Mode::Playing;
          break;
        case InGameMenuItem::RefreshScreen:
          triggerFullRefresh();
          currentMode = Mode::Playing;
          break;
        case InGameMenuItem::Exit:
          currentMode = Mode::PackMenu;
          break;
      }
      updateRequired = true;
    } else if (input_.wasPressed(HalGPIO::BTN_BACK)) {
      currentMode = Mode::Playing;
      updateRequired = true;
    }
    return;
  }

  if (input_.isPressed(HalGPIO::BTN_BACK) && input_.getHeldTime() >= IN_GAME_MENU_HOLD_MS) {
    inGameMenuIndex = 0;
    currentMode = Mode::InGameMenu;
    updateRequired = true;
    return;
  }

  if (puzzleSolved || puzzleFailed) {
    if (input_.wasReleased(HalGPIO::BTN_CONFIRM)) {
      if (puzzleSolved) {
        if (!activeTheme.empty() && !themeBitset.empty()) {
          loadRandomThemedPuzzle();
        } else {
          loadNextPuzzle();
        }
      } else {
        loadPuzzleFromPack(currentPuzzleIndex);
      }
      updateRequired = true;
      return;
    }
    if (input_.wasReleased(HalGPIO::BTN_BACK)) {
      currentMode = Mode::PackMenu;
      updateRequired = true;
    }
    return;
  }

  bool moved = false;

  if (!pieceSelected) {
    // Navigate between player pieces
    if (navigablePieces.empty()) {
      // Edge case: no player pieces, do nothing
    } else if (input_.wasPressed(HalGPIO::BTN_LEFT) || input_.wasPressed(HalGPIO::BTN_UP)) {
      navigablePieceIndex = (navigablePieceIndex - 1 + navigablePieces.size()) % navigablePieces.size();
      int sq = navigablePieces[navigablePieceIndex];
      cursorFile = Chess::BoardState::fileOf(sq);
      cursorRank = Chess::BoardState::rankOf(sq);
      moved = true;
    } else if (input_.wasPressed(HalGPIO::BTN_RIGHT) || input_.wasPressed(HalGPIO::BTN_DOWN)) {
      navigablePieceIndex = (navigablePieceIndex + 1) % navigablePieces.size();
      int sq = navigablePieces[navigablePieceIndex];
      cursorFile = Chess::BoardState::fileOf(sq);
      cursorRank = Chess::BoardState::rankOf(sq);
      moved = true;
    }
  } else {
    // Navigate between legal move destinations
    if (legalMovesFromSelected.empty()) {
      // Edge case: no legal moves, do nothing
    } else if (input_.wasPressed(HalGPIO::BTN_LEFT) || input_.wasPressed(HalGPIO::BTN_UP)) {
      legalMoveNavIndex = (legalMoveNavIndex - 1 + legalMovesFromSelected.size()) % legalMovesFromSelected.size();
      int destSq = legalMovesFromSelected[legalMoveNavIndex].to;
      cursorFile = Chess::BoardState::fileOf(destSq);
      cursorRank = Chess::BoardState::rankOf(destSq);
      moved = true;
    } else if (input_.wasPressed(HalGPIO::BTN_RIGHT) || input_.wasPressed(HalGPIO::BTN_DOWN)) {
      legalMoveNavIndex = (legalMoveNavIndex + 1) % legalMovesFromSelected.size();
      int destSq = legalMovesFromSelected[legalMoveNavIndex].to;
      cursorFile = Chess::BoardState::fileOf(destSq);
      cursorRank = Chess::BoardState::rankOf(destSq);
      moved = true;
    }
  }

  if (input_.wasReleased(HalGPIO::BTN_CONFIRM)) {
    int sq = cursorSquare();
    
    if (pieceSelected) {
      if (sq == selectedSquare) {
        deselectPiece();
      } else if (isLegalDestination(sq)) {
        for (const auto& move : legalMovesFromSelected) {
          if (move.to == sq) {
            handlePlayerMove(move);
            break;
          }
        }
      } else {
        selectSquare(sq);
      }
    } else {
      selectSquare(sq);
    }
    updateRequired = true;
  } else if (input_.wasReleased(HalGPIO::BTN_BACK)) {
    if (pieceSelected) {
      deselectPiece();
      updateRequired = true;
    } else {
      currentMode = Mode::PackMenu;
      updateRequired = true;
    }
  }

  if (moved) {
    updateRequired = true;
  }
}

void ChessPuzzlesApp::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void ChessPuzzlesApp::render() {
  auto& renderer = renderer_;

  renderer.clearScreen();
  
  if (currentMode == Mode::PackSelect) {
    renderPackSelect();
  } else if (currentMode == Mode::PackMenu) {
    renderPackMenu();
  } else if (currentMode == Mode::ThemeSelect) {
    renderThemeSelect();
  } else if (currentMode == Mode::Browsing) {
    renderBrowser();
  } else if (currentMode == Mode::InGameMenu) {
    renderInGameMenu();
  } else {
    renderBoard();
    renderLegalMoveHints();
    renderCursor();
    renderStatus();
    
    const char* btn2Label = "Select";
    if (puzzleSolved) {
      btn2Label = "Next";
    } else if (puzzleFailed) {
      btn2Label = "Retry";
    }
    renderer.drawButtonHints(UI_10_FONT_ID, "Menu", btn2Label, "<", ">");
  }
  
  if (pendingFullRefresh) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pendingFullRefresh = false;
  } else {
    renderer.displayBuffer();
  }
}

void ChessPuzzlesApp::renderInGameMenu() {
  auto& renderer = renderer_;

  // Keep the current position visible behind the menu.
  renderBoard();

  const int screenWidth = renderer.getScreenWidth();
  const int panelWidth = 280;
  const int panelHeight = 280;
  const int panelX = (screenWidth - panelWidth) / 2;
  const int panelY = BOARD_SIZE - panelHeight;

  // Solid background so text stays readable on top of the board.
  renderer.fillRect(panelX, panelY, panelWidth, panelHeight);

  const char* title = "Puzzle Menu";
  const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, title);
  renderer.drawText(UI_12_FONT_ID, (screenWidth - titleWidth) / 2, panelY + 18, title, false);

  const char* items[] = {"Retry", "Skip", "Hint", "Refresh", "Exit"};
  constexpr int itemCount = IN_GAME_MENU_ITEM_COUNT;

  const int itemStartY = panelY + 70;
  const int itemLineHeight = 36;
  const int itemTextX = panelX + 30;

  for (int i = 0; i < itemCount; i++) {
    const int y = itemStartY + i * itemLineHeight;

    if (i == inGameMenuIndex) {
      // Draw a simple outline to indicate selection.
      const int outlineX = panelX + 18;
      const int outlineY = y - 6;
      const int outlineW = panelWidth - 36;
      const int outlineH = itemLineHeight - 6;
      renderer.drawLine(outlineX, outlineY, outlineX + outlineW, outlineY, false);
      renderer.drawLine(outlineX, outlineY + outlineH, outlineX + outlineW, outlineY + outlineH, false);
      renderer.drawLine(outlineX, outlineY, outlineX, outlineY + outlineH, false);
      renderer.drawLine(outlineX + outlineW, outlineY, outlineX + outlineW, outlineY + outlineH, false);
    }

    renderer.drawText(UI_12_FONT_ID, itemTextX, y, items[i], false);
  }

  const char* footer = "Up/Down: choose";
  const int footerWidth = renderer.getTextWidth(UI_10_FONT_ID, footer);
  renderer.drawText(UI_10_FONT_ID, (screenWidth - footerWidth) / 2, panelY + panelHeight - 34, footer, false);

  renderer.drawButtonHints(UI_10_FONT_ID, "Back", "Select", "", "");
}

void ChessPuzzlesApp::renderBoard() {
  auto& renderer = renderer_;

  for (int rank = 0; rank < 8; rank++) {
    for (int file = 0; file < 8; file++) {
      int x = screenX(file);
      int y = screenY(rank);
      
      bool isLight = (file + rank) % 2 == 1;
      
      if (!isLight) {
        renderer.fillRect(x, y, SQUARE_SIZE, SQUARE_SIZE);
      }
      
      renderPiece(file, rank);
    }
  }
  
  renderer.drawRect(BOARD_OFFSET_X, BOARD_OFFSET_Y, BOARD_SIZE, BOARD_SIZE);
}

void ChessPuzzlesApp::renderPiece(int file, int rank) {
  auto& renderer = renderer_;

  int sq = rank * 8 + file;
  Chess::Piece piece = board.at(sq);
  
  if (piece == Chess::NONE) return;
  
  int x = screenX(file);
  int y = screenY(rank);
  
  bool squareIsLight = (file + rank) % 2 == 1;

  const int pieceType = Chess::pieceType(piece);
  const int outlineSpriteId = pieceType;
  const int filledSpriteId = pieceType + 6;

  int spriteId = outlineSpriteId;
  if (Chess::isWhite(piece)) {
    spriteId = squareIsLight ? outlineSpriteId : filledSpriteId;
  } else if (Chess::isBlack(piece)) {
    spriteId = squareIsLight ? filledSpriteId : outlineSpriteId;
  }

  const uint8_t* sprite = ChessSprites::getPieceSprite(spriteId);
  if (!sprite) return;

  const bool drawBlack = squareIsLight;

  for (int py = 0; py < ChessSprites::PIECE_SIZE; py++) {
    for (int px = 0; px < ChessSprites::PIECE_SIZE; px++) {
      int bitIndex = py * ChessSprites::PIECE_SIZE + px;
      int byteIndex = bitIndex / 8;
      int bitOffset = bitIndex % 8;
      bool spritePixel = (sprite[byteIndex] >> bitOffset) & 1;

      if (spritePixel) {
        renderer.drawPixel(x + px, y + py, drawBlack);
      }
    }
  }
}

void ChessPuzzlesApp::renderCursor() {
  auto& renderer = renderer_;

  int x = screenX(cursorFile);
  int y = screenY(cursorRank);
  
  constexpr int thickness = 3;
  constexpr int cornerLen = 15;
  
  bool squareIsLight = (cursorFile + cursorRank) % 2 == 1;
  bool cursorColor = squareIsLight;
  
  for (int t = 0; t < thickness; t++) {
    renderer.drawLine(x + t, y, x + t, y + cornerLen, cursorColor);
    renderer.drawLine(x, y + t, x + cornerLen, y + t, cursorColor);
    
    renderer.drawLine(x + SQUARE_SIZE - 1 - t, y, x + SQUARE_SIZE - 1 - t, y + cornerLen, cursorColor);
    renderer.drawLine(x + SQUARE_SIZE - cornerLen, y + t, x + SQUARE_SIZE - 1, y + t, cursorColor);
    
    renderer.drawLine(x + t, y + SQUARE_SIZE - cornerLen, x + t, y + SQUARE_SIZE - 1, cursorColor);
    renderer.drawLine(x, y + SQUARE_SIZE - 1 - t, x + cornerLen, y + SQUARE_SIZE - 1 - t, cursorColor);
    
    renderer.drawLine(x + SQUARE_SIZE - 1 - t, y + SQUARE_SIZE - cornerLen, 
                     x + SQUARE_SIZE - 1 - t, y + SQUARE_SIZE - 1, cursorColor);
    renderer.drawLine(x + SQUARE_SIZE - cornerLen, y + SQUARE_SIZE - 1 - t, 
                     x + SQUARE_SIZE - 1, y + SQUARE_SIZE - 1 - t, cursorColor);
  }
}

void ChessPuzzlesApp::renderLegalMoveHints() {
  auto& renderer = renderer_;

  if (!pieceSelected) return;
  
  constexpr int dotRadius = 8;
  
  for (const auto& move : legalMovesFromSelected) {
    int file = Chess::BoardState::fileOf(move.to);
    int rank = Chess::BoardState::rankOf(move.to);
    
    int centerX = screenX(file) + SQUARE_SIZE / 2;
    int centerY = screenY(rank) + SQUARE_SIZE / 2;
    
    bool squareIsLight = (file + rank) % 2 == 1;
    bool dotColor = squareIsLight;
    
    bool isCapture = board.at(move.to) != Chess::NONE;
    
    if (isCapture) {
      for (int dy = -dotRadius; dy <= dotRadius; dy++) {
        for (int dx = -dotRadius; dx <= dotRadius; dx++) {
          int dist = dx * dx + dy * dy;
          int innerRadius = dotRadius - 3;
          if (dist <= dotRadius * dotRadius && dist >= innerRadius * innerRadius) {
            renderer.drawPixel(centerX + dx, centerY + dy, dotColor);
          }
        }
      }
    } else {
      for (int dy = -dotRadius; dy <= dotRadius; dy++) {
        for (int dx = -dotRadius; dx <= dotRadius; dx++) {
          if (dx * dx + dy * dy <= dotRadius * dotRadius) {
            renderer.drawPixel(centerX + dx, centerY + dy, dotColor);
          }
        }
      }
    }
  }
}

void ChessPuzzlesApp::renderStatus() {
  auto& renderer = renderer_;

  int y = STATUS_Y;
  
  if (puzzleSolved) {
    renderer.drawCenteredText(UI_12_FONT_ID, y, "Correct!");
    renderer.drawCenteredText(UI_10_FONT_ID, y + 30, "Press Select for next puzzle");
  } else if (puzzleFailed) {
    renderer.drawCenteredText(UI_12_FONT_ID, y, "Incorrect");
    renderer.drawCenteredText(UI_10_FONT_ID, y + 30, "Press Select to retry");
  } else {
    const char* toMove = board.whiteToMove ? "White to move" : "Black to move";
    renderer.drawCenteredText(UI_10_FONT_ID, y, toMove);
    
    char ratingStr[32];
    snprintf(ratingStr, sizeof(ratingStr), "Rating: %d  (%d/%d)", 
             currentPuzzle.rating, currentPuzzleIndex + 1, puzzleCount);
    renderer.drawCenteredText(UI_10_FONT_ID, y + 25, ratingStr);
    
    if (board.inCheck()) {
      renderer.drawCenteredText(UI_10_FONT_ID, y + 50, "Check!");
    }
  }
}

void ChessPuzzlesApp::loadAvailablePacks() {
  availablePacks.clear();
  
  auto dir = SdMan.open("/.crosspoint/chess/packs");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }
  
  dir.rewindDirectory();
  
  char name[128];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    if (name[0] == '.') {
      file.close();
      continue;
    }
    
    std::string filename(name);
    if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".cpz") {
      availablePacks.push_back(filename);
    }
    file.close();
  }
  dir.close();
  
  std::sort(availablePacks.begin(), availablePacks.end());
  
  Serial.printf("[CHESS] Found %d puzzle packs\n", availablePacks.size());
}

void ChessPuzzlesApp::renderPackSelect() {
  auto& renderer = renderer_;

  renderer.drawCenteredText(UI_12_FONT_ID, 30, "Chess Puzzles");
  renderer.drawCenteredText(UI_10_FONT_ID, 60, "Select a puzzle pack:");
  
  if (availablePacks.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, 150, "No puzzle packs found!");
    renderer.drawCenteredText(UI_10_FONT_ID, 180, "Add .cpz files to:");
    renderer.drawCenteredText(UI_10_FONT_ID, 210, "/.crosspoint/chess/packs/");
  } else {
    constexpr int startY = 100;
    constexpr int lineHeight = 30;
    constexpr int maxVisible = 15;
    
    int startIdx = 0;
    if (packSelectorIndex >= maxVisible) {
      startIdx = packSelectorIndex - maxVisible + 1;
    }
    
    for (int i = 0; i < maxVisible && (startIdx + i) < static_cast<int>(availablePacks.size()); i++) {
      int idx = startIdx + i;
      int y = startY + i * lineHeight;
      
      std::string displayName = availablePacks[idx];
      if (displayName.size() > 4) {
        displayName = displayName.substr(0, displayName.size() - 4);
      }
      
      if (idx == packSelectorIndex) {
        int textWidth = renderer.getTextWidth(UI_12_FONT_ID, displayName.c_str());
        int screenWidth = renderer.getScreenWidth();
        int rectX = (screenWidth - textWidth) / 2 - 10;
        renderer.fillRect(rectX, y - 2, textWidth + 20, lineHeight - 4);
        renderer.drawText(UI_12_FONT_ID, rectX + 10, y, displayName.c_str(), false);
      } else {
        renderer.drawCenteredText(UI_10_FONT_ID, y, displayName.c_str());
      }
    }
    
    if (availablePacks.size() > maxVisible) {
      char scrollInfo[16];
      snprintf(scrollInfo, sizeof(scrollInfo), "%d/%d", packSelectorIndex + 1, static_cast<int>(availablePacks.size()));
      renderer.drawCenteredText(UI_10_FONT_ID, startY + maxVisible * lineHeight + 10, scrollInfo);
    }
  }
  
  renderer.drawButtonHints(UI_10_FONT_ID, "Exit", "Open", "", "");
}

bool ChessPuzzlesApp::loadPackInfo() {
  FsFile file;
  if (!SdMan.openFileForRead("CHESS", packPath, file)) {
    Serial.println("[CHESS] Failed to open pack file");
    return false;
  }
  
  uint8_t header[Chess::PACK_HEADER_SIZE];
  if (file.read(header, Chess::PACK_HEADER_SIZE) != Chess::PACK_HEADER_SIZE) {
    file.close();
    Serial.println("[CHESS] Failed to read pack header");
    return false;
  }
  file.close();
  
  Chess::PackHeader packHeader;
  if (!Chess::PackHeader::fromFile(header, packHeader)) {
    Serial.println("[CHESS] Invalid pack magic");
    return false;
  }
  
  puzzleCount = packHeader.puzzleCount;
  Serial.printf("[CHESS] Loaded pack with %d puzzles (rating %d-%d)\n", 
                puzzleCount, packHeader.ratingMin, packHeader.ratingMax);
  return true;
}

bool ChessPuzzlesApp::loadPuzzleFromPack(uint32_t index) {
  if (index >= puzzleCount) {
    return false;
  }
  
  FsFile file;
  if (!SdMan.openFileForRead("CHESS", packPath, file)) {
    return false;
  }
  
  uint32_t offset = Chess::PACK_HEADER_SIZE + index * Chess::RECORD_SIZE;
  if (!file.seek(offset)) {
    file.close();
    return false;
  }
  
  uint8_t record[Chess::RECORD_SIZE];
  if (file.read(record, Chess::RECORD_SIZE) != Chess::RECORD_SIZE) {
    file.close();
    return false;
  }
  file.close();
  
  currentPuzzle = Chess::Puzzle::fromRecord(record);
  currentPuzzleIndex = index;
  
  board = currentPuzzle.position;
  playerIsWhite = board.whiteToMove;
  currentMoveIndex = 0;
  puzzleSolved = false;
  puzzleFailed = false;
  hintActive = false;
  movesSinceFullRefresh = 0;
  pendingFullRefresh = false;

  deselectPiece();
  
  Serial.printf("[CHESS] Loaded puzzle %d, rating %d, %d moves\n",
                index, currentPuzzle.rating, currentPuzzle.solution.size());
  return true;
}

void ChessPuzzlesApp::loadNextPuzzle() {
  uint32_t nextIndex = (currentPuzzleIndex + 1) % puzzleCount;
  if (!loadPuzzleFromPack(nextIndex)) {
    loadDemoPuzzle();
  }
}

void ChessPuzzlesApp::loadDemoPuzzle() {
  for (int i = 0; i < 64; i++) {
    board.set(i, Chess::NONE);
  }
  
  board.set(Chess::BoardState::makeSquare(4, 0), Chess::W_KING);
  board.set(Chess::BoardState::makeSquare(7, 0), Chess::W_ROOK);
  board.set(Chess::BoardState::makeSquare(0, 1), Chess::W_PAWN);
  board.set(Chess::BoardState::makeSquare(1, 1), Chess::W_PAWN);
  board.set(Chess::BoardState::makeSquare(5, 1), Chess::W_PAWN);
  board.set(Chess::BoardState::makeSquare(6, 1), Chess::W_PAWN);
  board.set(Chess::BoardState::makeSquare(7, 1), Chess::W_PAWN);
  board.set(Chess::BoardState::makeSquare(2, 2), Chess::W_PAWN);
  board.set(Chess::BoardState::makeSquare(5, 2), Chess::W_KNIGHT);
  board.set(Chess::BoardState::makeSquare(3, 3), Chess::W_PAWN);
  
  board.set(Chess::BoardState::makeSquare(4, 7), Chess::B_KING);
  board.set(Chess::BoardState::makeSquare(0, 7), Chess::B_ROOK);
  board.set(Chess::BoardState::makeSquare(0, 6), Chess::B_PAWN);
  board.set(Chess::BoardState::makeSquare(1, 6), Chess::B_PAWN);
  board.set(Chess::BoardState::makeSquare(5, 6), Chess::B_PAWN);
  board.set(Chess::BoardState::makeSquare(6, 6), Chess::B_PAWN);
  board.set(Chess::BoardState::makeSquare(7, 6), Chess::B_PAWN);
  board.set(Chess::BoardState::makeSquare(2, 5), Chess::B_PAWN);
  board.set(Chess::BoardState::makeSquare(3, 4), Chess::B_PAWN);
  board.set(Chess::BoardState::makeSquare(3, 5), Chess::B_QUEEN);
  
  board.whiteToMove = true;
  board.castling = 0;
  board.epSquare = -1;
  
  playerIsWhite = true;
  
  currentPuzzle.rating = 1200;
  currentPuzzle.position = board;
  currentPuzzle.solution.clear();
  currentPuzzle.solution.push_back(Chess::Move(
    Chess::BoardState::makeSquare(5, 2),
    Chess::BoardState::makeSquare(4, 4)
  ));
  
  puzzleCount = 1;
  currentPuzzleIndex = 0;
  currentMoveIndex = 0;
  puzzleSolved = false;
  puzzleFailed = false;
  hintActive = false;
  movesSinceFullRefresh = 0;
  pendingFullRefresh = false;
  
  deselectPiece();
}

void ChessPuzzlesApp::selectSquare(int sq) {
  Chess::Piece piece = board.at(sq);

  bool isPlayerPiece = (playerIsWhite && Chess::isWhite(piece)) ||
                       (!playerIsWhite && Chess::isBlack(piece));

  if (!isPlayerPiece) {
    if (pieceSelected && isLegalDestination(sq)) {
      return;
    }
    deselectPiece();
    return;
  }

  pieceSelected = true;
  selectedSquare = sq;
  legalMovesFromSelected = board.generateLegalMovesFrom(sq);
  legalMoveNavIndex = 0;
}

void ChessPuzzlesApp::deselectPiece() {
  pieceSelected = false;
  selectedSquare = -1;
  legalMovesFromSelected.clear();
  buildNavigablePieceList();
}

bool ChessPuzzlesApp::tryMove(const Chess::Move& move) {
  if (!board.isLegalMove(move)) {
    return false;
  }
  
  board = board.applyMove(move);
  return true;
}

void ChessPuzzlesApp::handlePlayerMove(const Chess::Move& move) {
  if (currentMoveIndex >= static_cast<int>(currentPuzzle.solution.size())) {
    onPuzzleFailed();
    return;
  }
  
  const Chess::Move& expectedMove = currentPuzzle.solution[currentMoveIndex];
  
  if (move.from != expectedMove.from || move.to != expectedMove.to) {
    onPuzzleFailed();
    return;
  }
  
  if (!tryMove(move)) {
    onPuzzleFailed();
    return;
  }
  
  deselectPiece();
  currentMoveIndex++;
  
  movesSinceFullRefresh++;
  if (movesSinceFullRefresh >= 10) {
    pendingFullRefresh = true;
    movesSinceFullRefresh = 0;
  }
  
  if (currentMoveIndex >= static_cast<int>(currentPuzzle.solution.size())) {
    onPuzzleSolved();
    return;
  }
  
  playOpponentMove();
}

void ChessPuzzlesApp::playOpponentMove() {
  if (currentMoveIndex >= static_cast<int>(currentPuzzle.solution.size())) {
    return;
  }

  const Chess::Move& opponentMove = currentPuzzle.solution[currentMoveIndex];
  tryMove(opponentMove);
  currentMoveIndex++;

  buildNavigablePieceList();

  updateRequired = true;
}

void ChessPuzzlesApp::onPuzzleSolved() {
  puzzleSolved = true;
  markPuzzleSolved(currentPuzzleIndex);
  saveSolvedBitset();
  solvedCount++;
  saveProgress();
  updateRequired = true;
}

void ChessPuzzlesApp::onPuzzleFailed() {
  puzzleFailed = true;
  updateRequired = true;
}

int ChessPuzzlesApp::screenX(int file) const {
  if (playerIsWhite) {
    return BOARD_OFFSET_X + file * SQUARE_SIZE;
  } else {
    return BOARD_OFFSET_X + (7 - file) * SQUARE_SIZE;
  }
}

int ChessPuzzlesApp::screenY(int rank) const {
  if (playerIsWhite) {
    return BOARD_OFFSET_Y + (7 - rank) * SQUARE_SIZE;
  } else {
    return BOARD_OFFSET_Y + rank * SQUARE_SIZE;
  }
}

bool ChessPuzzlesApp::isLegalDestination(int sq) const {
  for (const auto& move : legalMovesFromSelected) {
    if (move.to == sq) return true;
  }
  return false;
}

void ChessPuzzlesApp::buildNavigablePieceList() {
  navigablePieces.clear();
  // Scan in BOARD COORDINATE order (0-63), NOT screen order
  // This means a1(0), b1(1)...h1(7), a2(8)...h8(63)
  // Same order regardless of playerIsWhite (board flip is visual only)
  for (int sq = 0; sq < 64; sq++) {
    Chess::Piece piece = board.at(sq);
    bool isPlayerPiece = (playerIsWhite && Chess::isWhite(piece)) ||
                         (!playerIsWhite && Chess::isBlack(piece));
    if (isPlayerPiece) {
      navigablePieces.push_back(sq);
    }
  }
  // Handle edge case: if list is empty (shouldn't happen in valid puzzles)
  // navigablePieceIndex will be clamped in navigation logic
  if (!navigablePieces.empty()) {
    navigablePieceIndex = 0;
    // Set cursor to first piece
    cursorFile = Chess::BoardState::fileOf(navigablePieces[0]);
    cursorRank = Chess::BoardState::rankOf(navigablePieces[0]);
  }
}

std::string ChessPuzzlesApp::getProgressPath() const {
  std::string packName;
  size_t lastSlash = packPath.rfind('/');
  if (lastSlash != std::string::npos) {
    packName = packPath.substr(lastSlash + 1);
  } else {
    packName = packPath;
  }
  if (packName.size() > 4 && packName.substr(packName.size() - 4) == ".cpz") {
    packName = packName.substr(0, packName.size() - 4);
  }
  return "/.crosspoint/chess/progress_" + packName + ".bin";
}

void ChessPuzzlesApp::saveProgress() {
  if (packPath.empty() || puzzleCount == 0) return;
  
  FsFile file;
  std::string progressPath = getProgressPath();
  if (!SdMan.openFileForWrite("CHESS", progressPath, file)) {
    Serial.printf("[CHESS] Failed to save progress to %s\n", progressPath.c_str());
    return;
  }
  
  uint8_t data[4];
  data[0] = currentPuzzleIndex & 0xFF;
  data[1] = (currentPuzzleIndex >> 8) & 0xFF;
  data[2] = (currentPuzzleIndex >> 16) & 0xFF;
  data[3] = (currentPuzzleIndex >> 24) & 0xFF;
  file.write(data, 4);
  file.close();
  
  Serial.printf("[CHESS] Saved progress: puzzle %d\n", currentPuzzleIndex);
}

uint32_t ChessPuzzlesApp::loadProgress() {
  if (packPath.empty()) return 0;
  
  FsFile file;
  std::string progressPath = getProgressPath();
  if (!SdMan.openFileForRead("CHESS", progressPath, file)) {
    Serial.printf("[CHESS] No saved progress found at %s\n", progressPath.c_str());
    return 0;
  }
  
  uint8_t data[4];
  if (file.read(data, 4) != 4) {
    file.close();
    return 0;
  }
  file.close();
  
  uint32_t savedIndex = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
  Serial.printf("[CHESS] Loaded progress: puzzle %d\n", savedIndex);
  return savedIndex;
}

void ChessPuzzlesApp::renderPackMenu() {
  auto& renderer = renderer_;

  renderer.drawCenteredText(UI_12_FONT_ID, 30, packName.c_str());
  
  char statsStr[64];
  snprintf(statsStr, sizeof(statsStr), "%d puzzles  |  %d solved", puzzleCount, solvedCount);
  renderer.drawCenteredText(UI_10_FONT_ID, 60, statsStr);
  
  constexpr int startY = 130;
  constexpr int lineHeight = 38;
  constexpr int menuWidth = 200;
  
  const char* menuItems[] = { "Continue", "Random Puzzle", "By Theme", "Browse All" };
  int screenWidth = renderer.getScreenWidth();
  int menuX = (screenWidth - menuWidth) / 2;
  
  for (int i = 0; i < PACK_MENU_ITEM_COUNT; i++) {
    int y = startY + i * lineHeight;
    
    if (i == packMenuIndex) {
      renderer.fillRect(menuX, y - 2, menuWidth, lineHeight - 8);
      int textWidth = renderer.getTextWidth(UI_12_FONT_ID, menuItems[i]);
      int textX = (screenWidth - textWidth) / 2;
      renderer.drawText(UI_12_FONT_ID, textX, y, menuItems[i], false);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, y, menuItems[i]);
    }
  }
  
  renderer.drawButtonHints(UI_10_FONT_ID, "Back", "Select", "", "");
}

void ChessPuzzlesApp::renderBrowser() {
  auto& renderer = renderer_;

  renderer.drawCenteredText(UI_12_FONT_ID, 20, "Browse Puzzles");
  
  char headerStr[64];
  snprintf(headerStr, sizeof(headerStr), "%s  (%d/%d solved)", 
           packName.c_str(), solvedCount, puzzleCount);
  renderer.drawCenteredText(UI_10_FONT_ID, 50, headerStr);
  
  constexpr int startY = 90;
  constexpr int lineHeight = 28;
  constexpr int maxVisible = 14;
  constexpr int itemWidth = 420;
  
  int screenWidth = renderer.getScreenWidth();
  int listX = (screenWidth - itemWidth) / 2;
  
  uint32_t startIdx = 0;
  if (browserIndex >= maxVisible) {
    startIdx = browserIndex - maxVisible + 1;
  }
  
  for (int i = 0; i < maxVisible && (startIdx + i) < puzzleCount; i++) {
    uint32_t idx = startIdx + i;
    int y = startY + i * lineHeight;
    
    bool solved = isPuzzleSolved(idx);
    
    char itemStr[64];
    snprintf(itemStr, sizeof(itemStr), "%s #%d", solved ? "[x]" : "[ ]", idx + 1);
    
    if (idx == browserIndex) {
      renderer.fillRect(listX, y - 2, itemWidth, lineHeight - 4);
      renderer.drawText(UI_12_FONT_ID, listX + 10, y, itemStr, false);
    } else {
      renderer.drawText(UI_10_FONT_ID, listX + 10, y, itemStr, true);
    }
  }
  
  char scrollInfo[32];
  snprintf(scrollInfo, sizeof(scrollInfo), "%d / %d", browserIndex + 1, puzzleCount);
  renderer.drawCenteredText(UI_10_FONT_ID, startY + maxVisible * lineHeight + 10, scrollInfo);
  
  renderer.drawButtonHints(UI_10_FONT_ID, "Back", "Play", "-10", "+10");
}

std::string ChessPuzzlesApp::getSolvedPath() const {
  return "/.crosspoint/chess/progress/" + packName + ".done";
}

void ChessPuzzlesApp::loadSolvedBitset() {
  solvedBitset.clear();
  
  if (puzzleCount == 0) return;
  
  size_t bitsetSize = (puzzleCount + 7) / 8;
  solvedBitset.resize(bitsetSize, 0);
  
  FsFile file;
  std::string solvedPath = getSolvedPath();
  if (!SdMan.openFileForRead("CHESS", solvedPath, file)) {
    Serial.printf("[CHESS] No solved bitset found at %s, starting fresh\n", solvedPath.c_str());
    return;
  }
  
  size_t bytesRead = file.read(solvedBitset.data(), bitsetSize);
  file.close();
  
  if (bytesRead != bitsetSize) {
    Serial.printf("[CHESS] Solved bitset size mismatch, resetting\n");
    std::fill(solvedBitset.begin(), solvedBitset.end(), 0);
  } else {
    Serial.printf("[CHESS] Loaded solved bitset (%d bytes)\n", bytesRead);
  }
}

void ChessPuzzlesApp::saveSolvedBitset() {
  if (solvedBitset.empty() || packName.empty()) return;
  
  SdMan.mkdir("/.crosspoint/chess/progress");
  
  FsFile file;
  std::string solvedPath = getSolvedPath();
  if (!SdMan.openFileForWrite("CHESS", solvedPath, file)) {
    Serial.printf("[CHESS] Failed to save solved bitset to %s\n", solvedPath.c_str());
    return;
  }
  
  file.write(solvedBitset.data(), solvedBitset.size());
  file.close();
  
  Serial.printf("[CHESS] Saved solved bitset (%d bytes)\n", solvedBitset.size());
}

void ChessPuzzlesApp::markPuzzleSolved(uint32_t index) {
  if (index / 8 >= solvedBitset.size()) return;
  solvedBitset[index / 8] |= (1 << (index % 8));
}

bool ChessPuzzlesApp::isPuzzleSolved(uint32_t index) const {
  if (index / 8 >= solvedBitset.size()) return false;
  return (solvedBitset[index / 8] >> (index % 8)) & 1;
}

void ChessPuzzlesApp::countSolvedPuzzles() {
  solvedCount = 0;
  for (uint32_t i = 0; i < puzzleCount; i++) {
    if (isPuzzleSolved(i)) {
      solvedCount++;
    }
  }
  Serial.printf("[CHESS] Solved count: %d/%d\n", solvedCount, puzzleCount);
}

void ChessPuzzlesApp::loadRandomPuzzle() {
  if (puzzleCount == 0) {
    loadDemoPuzzle();
    return;
  }
  
  uint32_t unsolvedCount = puzzleCount - solvedCount;
  if (unsolvedCount == 0) {
    uint32_t randomIndex = esp_random() % puzzleCount;
    if (!loadPuzzleFromPack(randomIndex)) {
      loadDemoPuzzle();
    }
    return;
  }
  
  uint32_t targetUnsolved = esp_random() % unsolvedCount;
  uint32_t unseenCount = 0;
  
  for (uint32_t i = 0; i < puzzleCount; i++) {
    if (!isPuzzleSolved(i)) {
      if (unseenCount == targetUnsolved) {
        if (loadPuzzleFromPack(i)) {
          return;
        }
        break;
      }
      unseenCount++;
    }
  }
  
  loadDemoPuzzle();
}

void ChessPuzzlesApp::loadAvailableThemes() {
  availableThemes.clear();
  
  std::string indexDir = "/.crosspoint/chess/index/" + packName;
  auto dir = SdMan.open(indexDir.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    Serial.printf("[CHESS] No theme index directory found at %s\n", indexDir.c_str());
    return;
  }
  
  dir.rewindDirectory();
  
  char name[128];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    if (name[0] == '.') {
      file.close();
      continue;
    }
    
    std::string filename(name);
    if (filename.size() > 4 && filename.substr(0, 6) == "theme_" && 
        filename.substr(filename.size() - 4) == ".bit") {
      std::string themeName = filename.substr(6, filename.size() - 10);
      availableThemes.push_back(themeName);
    }
    file.close();
  }
  dir.close();
  
  std::sort(availableThemes.begin(), availableThemes.end());
  Serial.printf("[CHESS] Found %d themes for pack %s\n", availableThemes.size(), packName.c_str());
}

void ChessPuzzlesApp::loadThemeBitset(const std::string& theme) {
  themeBitset.clear();
  
  if (puzzleCount == 0) return;
  
  size_t bitsetSize = (puzzleCount + 7) / 8;
  themeBitset.resize(bitsetSize, 0);
  
  std::string themePath = "/.crosspoint/chess/index/" + packName + "/theme_" + theme + ".bit";
  
  FsFile file;
  if (!SdMan.openFileForRead("CHESS", themePath, file)) {
    Serial.printf("[CHESS] Failed to load theme bitset from %s\n", themePath.c_str());
    themeBitset.clear();
    return;
  }
  
  size_t bytesRead = file.read(themeBitset.data(), bitsetSize);
  file.close();
  
  if (bytesRead != bitsetSize) {
    Serial.printf("[CHESS] Theme bitset size mismatch\n");
    themeBitset.clear();
  } else {
    Serial.printf("[CHESS] Loaded theme %s bitset (%d bytes)\n", theme.c_str(), bytesRead);
  }
}

bool ChessPuzzlesApp::puzzleMatchesTheme(uint32_t index) const {
  if (themeBitset.empty()) return true;
  if (index / 8 >= themeBitset.size()) return false;
  return (themeBitset[index / 8] >> (index % 8)) & 1;
}

void ChessPuzzlesApp::loadRandomThemedPuzzle() {
  if (puzzleCount == 0 || themeBitset.empty()) {
    loadRandomPuzzle();
    return;
  }
  
  uint32_t matchingCount = 0;
  for (uint32_t i = 0; i < puzzleCount; i++) {
    if (puzzleMatchesTheme(i) && !isPuzzleSolved(i)) {
      matchingCount++;
    }
  }
  
  if (matchingCount == 0) {
    for (uint32_t i = 0; i < puzzleCount; i++) {
      if (puzzleMatchesTheme(i)) {
        matchingCount++;
      }
    }
    
    if (matchingCount == 0) {
      loadRandomPuzzle();
      return;
    }
    
    uint32_t target = esp_random() % matchingCount;
    uint32_t count = 0;
    for (uint32_t i = 0; i < puzzleCount; i++) {
      if (puzzleMatchesTheme(i)) {
        if (count == target) {
          loadPuzzleFromPack(i);
          return;
        }
        count++;
      }
    }
  } else {
    uint32_t target = esp_random() % matchingCount;
    uint32_t count = 0;
    for (uint32_t i = 0; i < puzzleCount; i++) {
      if (puzzleMatchesTheme(i) && !isPuzzleSolved(i)) {
        if (count == target) {
          loadPuzzleFromPack(i);
          return;
        }
        count++;
      }
    }
  }
  
  loadRandomPuzzle();
}

void ChessPuzzlesApp::triggerFullRefresh() {
  pendingFullRefresh = true;
  updateRequired = true;
}

void ChessPuzzlesApp::renderThemeSelect() {
  auto& renderer = renderer_;

  renderer.drawCenteredText(UI_12_FONT_ID, 30, "Select Theme");
  
  char headerStr[64];
  snprintf(headerStr, sizeof(headerStr), "%s", packName.c_str());
  renderer.drawCenteredText(UI_10_FONT_ID, 60, headerStr);
  
  if (availableThemes.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, 170, "No themes available");
    renderer.drawCenteredText(UI_10_FONT_ID, 200, "Copy index folder to:");
    renderer.drawCenteredText(UI_10_FONT_ID, 230, "/.crosspoint/chess/index/");
    renderer.drawCenteredText(UI_10_FONT_ID, 260, packName.c_str());
  } else {
    constexpr int startY = 100;
    constexpr int lineHeight = 28;
    constexpr int maxVisible = 14;
    constexpr int itemWidth = 300;
    
    int screenWidth = renderer.getScreenWidth();
    int listX = (screenWidth - itemWidth) / 2;
    
    int startIdx = 0;
    if (themeSelectIndex >= maxVisible) {
      startIdx = themeSelectIndex - maxVisible + 1;
    }
    
    for (int i = 0; i < maxVisible && (startIdx + i) < static_cast<int>(availableThemes.size()); i++) {
      int idx = startIdx + i;
      int y = startY + i * lineHeight;
      
      const std::string& theme = availableThemes[idx];
      
      if (idx == themeSelectIndex) {
        renderer.fillRect(listX, y - 2, itemWidth, lineHeight - 4);
        renderer.drawText(UI_12_FONT_ID, listX + 10, y, theme.c_str(), false);
      } else {
        renderer.drawText(UI_10_FONT_ID, listX + 10, y, theme.c_str(), true);
      }
    }
    
    if (availableThemes.size() > maxVisible) {
      char scrollInfo[16];
      snprintf(scrollInfo, sizeof(scrollInfo), "%d/%d", themeSelectIndex + 1, static_cast<int>(availableThemes.size()));
      renderer.drawCenteredText(UI_10_FONT_ID, startY + maxVisible * lineHeight + 10, scrollInfo);
    }
  }
  
  renderer.drawButtonHints(UI_10_FONT_ID, "Back", "Select", "", "");
}

void ChessPuzzlesApp::renderSdCardError() {
  auto& renderer = renderer_;

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 160, "SD card error");
  renderer.drawCenteredText(UI_10_FONT_ID, 200, "Insert SD card and reboot");
  renderer.drawButtonHints(UI_10_FONT_ID, "Exit", "", "", "");
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void ChessPuzzlesApp::returnToLauncher() {
  esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL));
  esp_restart();
}
