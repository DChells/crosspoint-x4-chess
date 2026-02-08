#include "ChessPuzzlesApp.h"

#include <Arduino.h>
#include <SDCardManager.h>

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <builtinFonts/all.h>

#include <esp_ota_ops.h>
#include <esp_system.h>

#include <algorithm>
#include <cstdarg>

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
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  Serial.printf("[CHESS] BOOT: Running partition=%s addr=0x%08X\n",
                running ? running->label : "null", running ? running->address : 0);
  Serial.printf("[CHESS] BOOT: Boot partition=%s addr=0x%08X\n",
                boot ? boot->label : "null", boot ? boot->address : 0);

  const esp_partition_t* ota0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  const esp_partition_t* ota1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
  Serial.printf("[CHESS] BOOT: OTA_0=%s OTA_1=%s\n",
                ota0 ? "present" : "missing", ota1 ? "present" : "missing");

  display_.begin();
  renderer_.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer_.insertFont(UI_12_FONT_ID, ui12FontFamily);

  if (!SdMan.begin()) {
    renderSdCardError();
    return;
  }

  // Ensure expected SD directory structure exists.
  SdMan.mkdir("/.crosspoint/chess/packs");
  SdMan.mkdir("/.crosspoint/chess/index");
  SdMan.mkdir("/.crosspoint/chess/progress");

  if (!ChessSprites::loadSprites()) {
    Serial.println("[CHESS] Failed to load sprites from SD card");
  }

  renderingMutex = xSemaphoreCreateMutex();

  currentMode = Mode::MainMenu;
  mainMenuIndex = 0;
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

void ChessPuzzlesApp::logEvent(const char* ev, const char* fmt, ...) const {
  char msg[196];
  msg[0] = '\0';

  if (fmt) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
  }

  if (msg[0] != '\0') {
    Serial.printf("[%lu] [CHESS] %s %s\n", millis(), ev, msg);
  } else {
    Serial.printf("[%lu] [CHESS] %s\n", millis(), ev);
  }
}

void ChessPuzzlesApp::logModeChange(Mode from, Mode to, const char* reason) {
  auto modeName = [](Mode m) {
    switch (m) {
      case Mode::PackSelect:
        return "PackSelect";
      case Mode::PackMenu:
        return "PackMenu";
      case Mode::ThemeSelect:
        return "ThemeSelect";
      case Mode::Browsing:
        return "Browsing";
      case Mode::Playing:
        return "Playing";
      case Mode::InGameMenu:
        return "InGameMenu";
    }
    return "?";
  };

  logEvent("MODE", "%s -> %s (%s)", modeName(from), modeName(to), reason ? reason : "");
}

void ChessPuzzlesApp::renderMainMenu() {
  auto& renderer = renderer_;
  int screenWidth = renderer.getScreenWidth();

  renderer.drawCenteredText(UI_12_FONT_ID, 30, "Chess");

  constexpr int startY = 130;
  constexpr int lineHeight = 38;

  const char* items[] = { "Puzzles", "1v1 (Coming Soon)", "vs Bot (Coming Soon)" };
  constexpr int itemCount = MAIN_MENU_ITEM_COUNT;

  for (int i = 0; i < itemCount; i++) {
    int y = startY + i * lineHeight;

    if (i == mainMenuIndex) {
      int textWidth = renderer.getTextWidth(UI_12_FONT_ID, items[i]);
      int rectX = (screenWidth - textWidth) / 2 - MENU_HIGHLIGHT_PADDING;
      renderer.fillRect(rectX, y - 2, textWidth + MENU_HIGHLIGHT_PADDING * 2, lineHeight - 8);
      renderer.drawText(UI_12_FONT_ID, rectX + MENU_HIGHLIGHT_PADDING, y, items[i], false);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, y, items[i]);
    }
  }

  renderer.drawButtonHints(UI_10_FONT_ID, "Exit", "Select", "", "");
}

void ChessPuzzlesApp::loop() {
  auto logBtn = [this](const char* btn) {
    auto modeName = [](Mode m) {
      switch (m) {
        case Mode::MainMenu: return "MainMenu";
        case Mode::PackSelect: return "PackSelect";
        case Mode::PackMenu: return "PackMenu";
        case Mode::ThemeSelect: return "ThemeSelect";
        case Mode::Browsing: return "Browsing";
        case Mode::Playing: return "Playing";
        case Mode::InGameMenu: return "InGameMenu";
      }
      return "?";
    };
    logEvent("INPUT", "%s pressed in mode=%s", btn, modeName(currentMode));
  };

  if (currentMode == Mode::MainMenu) {
    if (input_.wasPressed(HalGPIO::BTN_UP)) { logBtn("UP"); if (mainMenuIndex > 0) { mainMenuIndex--; updateRequired = true; } }
    else if (input_.wasPressed(HalGPIO::BTN_LEFT)) { logBtn("LEFT"); if (mainMenuIndex > 0) { mainMenuIndex--; updateRequired = true; } }
    else if (input_.wasPressed(HalGPIO::BTN_DOWN)) { logBtn("DOWN"); if (mainMenuIndex < MAIN_MENU_ITEM_COUNT - 1) { mainMenuIndex++; updateRequired = true; } }
    else if (input_.wasPressed(HalGPIO::BTN_RIGHT)) { logBtn("RIGHT"); if (mainMenuIndex < MAIN_MENU_ITEM_COUNT - 1) { mainMenuIndex++; updateRequired = true; } }
    else if (input_.wasReleased(HalGPIO::BTN_CONFIRM)) {
      logBtn("CONFIRM");
      switch (static_cast<MainMenuItem>(mainMenuIndex)) {
        case MainMenuItem::Puzzles:
          logModeChange(currentMode, Mode::PackSelect, "puzzles selected");
          currentMode = Mode::PackSelect;
          updateRequired = true;
          break;
        case MainMenuItem::OneVsOne:
        case MainMenuItem::VsBot:
          renderer_.drawCenteredText(UI_10_FONT_ID, 250, "Coming Soon");
          renderer_.displayBuffer();
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          updateRequired = true;
          break;
      }
    } else if (input_.wasReleased(HalGPIO::BTN_BACK)) {
      logBtn("BACK");
      logEvent("EXIT", "from=MainMenu");
      returnToLauncher();
    }
    return;
  }

  if (currentMode == Mode::PackSelect) {
    if (input_.wasPressed(HalGPIO::BTN_UP)) { logBtn("UP"); if (packSelectorIndex > 0) { packSelectorIndex--; updateRequired = true; } }
    else if (input_.wasPressed(HalGPIO::BTN_LEFT)) { logBtn("LEFT"); if (packSelectorIndex > 0) { packSelectorIndex--; updateRequired = true; } }
    else if (input_.wasPressed(HalGPIO::BTN_DOWN)) { logBtn("DOWN"); if (packSelectorIndex < static_cast<int>(availablePacks.size()) - 1) { packSelectorIndex++; updateRequired = true; } }
    else if (input_.wasPressed(HalGPIO::BTN_RIGHT)) { logBtn("RIGHT"); if (packSelectorIndex < static_cast<int>(availablePacks.size()) - 1) { packSelectorIndex++; updateRequired = true; } }
    else if (input_.wasReleased(HalGPIO::BTN_CONFIRM)) {
      logBtn("CONFIRM");
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
          logModeChange(currentMode, Mode::PackMenu, "pack opened");
          currentMode = Mode::PackMenu;
        } else {
          loadDemoPuzzle();
          logModeChange(currentMode, Mode::Playing, "demo puzzle");
          currentMode = Mode::Playing;
        }
        logEvent("PACK", "name=%s", packName.c_str());
        updateRequired = true;
      }
    } else if (input_.wasReleased(HalGPIO::BTN_BACK)) {
      logBtn("BACK");
      logModeChange(currentMode, Mode::MainMenu, "back");
      currentMode = Mode::MainMenu;
      updateRequired = true;
    }
    return;
  }

  if (currentMode == Mode::PackMenu) {
    if (input_.wasPressed(HalGPIO::BTN_UP)) { logBtn("UP"); if (packMenuIndex > 0) { packMenuIndex--; updateRequired = true; } }
    else if (input_.wasPressed(HalGPIO::BTN_LEFT)) { logBtn("LEFT"); if (packMenuIndex > 0) { packMenuIndex--; updateRequired = true; } }
    else if (input_.wasPressed(HalGPIO::BTN_DOWN)) { logBtn("DOWN"); if (packMenuIndex < PACK_MENU_ITEM_COUNT - 1) { packMenuIndex++; updateRequired = true; } }
    else if (input_.wasPressed(HalGPIO::BTN_RIGHT)) { logBtn("RIGHT"); if (packMenuIndex < PACK_MENU_ITEM_COUNT - 1) { packMenuIndex++; updateRequired = true; } }
    else if (input_.wasReleased(HalGPIO::BTN_CONFIRM)) {
      logBtn("CONFIRM");
      switch (static_cast<PackMenuItem>(packMenuIndex)) {
        case PackMenuItem::Continue: {
          uint32_t savedIndex = loadProgress();
          if (savedIndex >= puzzleCount) savedIndex = 0;
          activeTheme.clear();
          themeBitset.clear();
           if (loadPuzzleFromPack(savedIndex)) {
             logModeChange(currentMode, Mode::Playing, "continue");
             currentMode = Mode::Playing;
           }
           break;
         }
        case PackMenuItem::Random:
          activeTheme.clear();
          themeBitset.clear();
          loadRandomPuzzle();
          logModeChange(currentMode, Mode::Playing, "random");
          currentMode = Mode::Playing;
          break;
        case PackMenuItem::Themes:
          loadAvailableThemes();
          themeSelectIndex = 0;
          logModeChange(currentMode, Mode::ThemeSelect, "themes");
          currentMode = Mode::ThemeSelect;
          break;
        case PackMenuItem::Browse: {
          browserIndex = loadProgress();
          if (browserIndex >= puzzleCount) browserIndex = 0;
          activeTheme.clear();
          themeBitset.clear();
          logModeChange(currentMode, Mode::Browsing, "browse");
          currentMode = Mode::Browsing;
            break;
          }
      }
      updateRequired = true;
    } else if (input_.wasReleased(HalGPIO::BTN_BACK)) {
      logBtn("BACK");
      logModeChange(currentMode, Mode::PackSelect, "back");
      currentMode = Mode::PackSelect;
      updateRequired = true;
    }
    return;
  }

  if (currentMode == Mode::ThemeSelect) {
    if (input_.wasPressed(HalGPIO::BTN_UP)) { logBtn("UP"); if (themeSelectIndex > 0) { themeSelectIndex--; updateRequired = true; } }
    else if (input_.wasPressed(HalGPIO::BTN_LEFT)) { logBtn("LEFT"); if (themeSelectIndex > 0) { themeSelectIndex--; updateRequired = true; } }
    else if (input_.wasPressed(HalGPIO::BTN_DOWN)) { logBtn("DOWN"); if (themeSelectIndex < static_cast<int>(availableThemes.size()) - 1) { themeSelectIndex++; updateRequired = true; } }
    else if (input_.wasPressed(HalGPIO::BTN_RIGHT)) { logBtn("RIGHT"); if (themeSelectIndex < static_cast<int>(availableThemes.size()) - 1) { themeSelectIndex++; updateRequired = true; } }
    else if (input_.wasReleased(HalGPIO::BTN_CONFIRM)) {
      logBtn("CONFIRM");
      if (!availableThemes.empty()) {
        activeTheme = availableThemes[themeSelectIndex];
        loadThemeBitset(activeTheme);
        loadRandomThemedPuzzle();
        logEvent("THEME", "selected=%s", activeTheme.c_str());
        logModeChange(currentMode, Mode::Playing, "theme selected");
        currentMode = Mode::Playing;
        updateRequired = true;
      }
    } else if (input_.wasReleased(HalGPIO::BTN_BACK)) {
      logBtn("BACK");
      logModeChange(currentMode, Mode::PackMenu, "back");
      currentMode = Mode::PackMenu;
      updateRequired = true;
    }
    return;
  }

  if (currentMode == Mode::Browsing) {
    if (input_.wasPressed(HalGPIO::BTN_UP)) {
      logBtn("UP");
      if (loadPrevPuzzleFromPack()) { updateRequired = true; }
    } else if (input_.wasPressed(HalGPIO::BTN_DOWN)) {
      logBtn("DOWN");
      if (loadNextPuzzleFromPack()) { updateRequired = true; }
    } else if (input_.wasPressed(HalGPIO::BTN_LEFT)) {
      logBtn("LEFT");
      for (int i = 0; i < 10; i++) { if (!loadPrevPuzzleFromPack()) break; }
      updateRequired = true;
    } else if (input_.wasPressed(HalGPIO::BTN_RIGHT)) {
      logBtn("RIGHT");
      for (int i = 0; i < 10; i++) { if (!loadNextPuzzleFromPack()) break; }
      updateRequired = true;
    } else if (input_.wasReleased(HalGPIO::BTN_CONFIRM)) {
      logBtn("CONFIRM");
      logModeChange(currentMode, Mode::Playing, "start playing");
      currentMode = Mode::Playing;
      updateRequired = true;
    } else if (input_.wasReleased(HalGPIO::BTN_BACK)) {
      logBtn("BACK");
      logModeChange(currentMode, Mode::PackMenu, "back");
      currentMode = Mode::PackMenu;
      updateRequired = true;
    }
    return;
  }

  if (currentMode == Mode::Playing) {
    if (input_.wasPressed(HalGPIO::BTN_UP)) { logBtn("UP"); if (inGameMenuIndex > 0) { inGameMenuIndex--; updateRequired = true; } }
    else if (input_.wasPressed(HalGPIO::BTN_LEFT)) { logBtn("LEFT"); if (inGameMenuIndex > 0) { inGameMenuIndex--; updateRequired = true; } }
    else if (input_.wasPressed(HalGPIO::BTN_DOWN)) { logBtn("DOWN"); if (inGameMenuIndex < IN_GAME_MENU_ITEM_COUNT - 1) { inGameMenuIndex++; updateRequired = true; } }
    else if (input_.wasPressed(HalGPIO::BTN_RIGHT)) { logBtn("RIGHT"); if (inGameMenuIndex < IN_GAME_MENU_ITEM_COUNT - 1) { inGameMenuIndex++; updateRequired = true; } }
    else if (input_.wasReleased(HalGPIO::BTN_CONFIRM)) {
      logBtn("CONFIRM");
      switch (static_cast<InGameMenuItem>(inGameMenuIndex)) {
        case InGameMenuItem::Retry:
          loadPuzzleFromPack(currentPuzzleIndex);
          logModeChange(currentMode, Mode::Playing, "retry");
          currentMode = Mode::Playing;
          break;
        case InGameMenuItem::Skip:
          if (!activeTheme.empty() && !themeBitset.empty()) {
            loadRandomThemedPuzzle();
          } else {
            loadNextPuzzle();
          }
          logModeChange(currentMode, Mode::Playing, "skip");
          currentMode = Mode::Playing;
          break;
        case InGameMenuItem::Hint:
          hintActive = true;
          logEvent("HINT", "active=1");
          currentMode = Mode::Playing;
          break;
        case InGameMenuItem::RefreshScreen:
          triggerFullRefresh();
          logModeChange(currentMode, Mode::Playing, "refresh");
          currentMode = Mode::Playing;
          break;
        case InGameMenuItem::Exit:
          logModeChange(currentMode, Mode::PackMenu, "exit to pack menu");
          currentMode = Mode::PackMenu;
          break;
      }
      updateRequired = true;
    } else if (input_.wasReleased(HalGPIO::BTN_BACK)) {
      if (ignoreBackRelease) {
        ignoreBackRelease = false;
        logEvent("BACK", "ignored release after hold");
      } else {
        logModeChange(currentMode, Mode::Playing, "back");
        currentMode = Mode::Playing;
        updateRequired = true;
      }
    }
    return;
  }

  if (input_.isPressed(HalGPIO::BTN_BACK) && input_.getHeldTime() >= IN_GAME_MENU_HOLD_MS) {
    inGameMenuIndex = 0;
    ignoreBackRelease = true;
    logModeChange(currentMode, Mode::InGameMenu, "hold menu");
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
    // Navigate between player pieces (screen-space grid navigation)
    if (!navigablePieces.empty()) {
      const bool goUp = input_.wasPressed(HalGPIO::BTN_UP);
      const bool goDown = input_.wasPressed(HalGPIO::BTN_DOWN);
      const bool goLeft = input_.wasPressed(HalGPIO::BTN_LEFT);
      const bool goRight = input_.wasPressed(HalGPIO::BTN_RIGHT);

      if (goUp || goDown || goLeft || goRight) {
        const int curSq = cursorSquare();
        const int curFile = Chess::BoardState::fileOf(curSq);
        const int curRank = Chess::BoardState::rankOf(curSq);
        const int curX = screenX(curFile) + SQUARE_SIZE / 2;
        const int curY = screenY(curRank) + SQUARE_SIZE / 2;

        int bestSq = -1;
        int bestPrimary = 0;
        int bestSecondary = 0;
        bool found = false;

        auto consider = [&](int sq, int primary, int secondary) {
          if (!found || primary < bestPrimary || (primary == bestPrimary && secondary < bestSecondary) ||
              (primary == bestPrimary && secondary == bestSecondary && sq < bestSq)) {
            bestSq = sq;
            bestPrimary = primary;
            bestSecondary = secondary;
            found = true;
          }
        };

        auto scan = [&](bool wrap, int forcedUp = 0, int forcedDown = 0, int forcedLeft = 0, int forcedRight = 0) {
          found = false;

          const bool up = forcedUp ? (forcedUp > 0) : goUp;
          const bool down = forcedDown ? (forcedDown > 0) : goDown;
          const bool left = forcedLeft ? (forcedLeft > 0) : goLeft;
          const bool right = forcedRight ? (forcedRight > 0) : goRight;

          for (int sq : navigablePieces) {
            if (sq == curSq) continue;
            const int f = Chess::BoardState::fileOf(sq);
            const int r = Chess::BoardState::rankOf(sq);
            const int x = screenX(f) + SQUARE_SIZE / 2;
            const int y = screenY(r) + SQUARE_SIZE / 2;
            const int dx = x - curX;
            const int dy = y - curY;

            if (up) {
              if (!wrap && dy >= 0) continue;
              if (wrap && dy <= 0) continue;
              consider(sq, wrap ? -dy : -dy, dx < 0 ? -dx : dx);
            } else if (down) {
              if (!wrap && dy <= 0) continue;
              if (wrap && dy >= 0) continue;
              consider(sq, wrap ? dy : dy, dx < 0 ? -dx : dx);
            } else if (left) {
              if (!wrap && dx >= 0) continue;
              if (wrap && dx <= 0) continue;
              consider(sq, wrap ? -dx : -dx, dy < 0 ? -dy : dy);
            } else if (right) {
              if (!wrap && dx <= 0) continue;
              if (wrap && dx >= 0) continue;
              consider(sq, wrap ? dx : dx, dy < 0 ? -dy : dy);
            }
          }
        };

        scan(false);
        if (!found) {
          if (goLeft) {
            logEvent("NAV", "Wrapping LEFT->UP");
            scan(false, 1, 0, 0, 0);
          } else if (goRight) {
            logEvent("NAV", "Wrapping RIGHT->DOWN");
            scan(false, 0, 1, 0, 0);
          } else if (goUp) {
            logEvent("NAV", "Wrapping UP->LEFT");
            scan(false, 0, 0, 1, 0);
          } else if (goDown) {
            logEvent("NAV", "Wrapping DOWN->RIGHT");
            scan(false, 0, 0, 0, 1);
          }
        }
        if (!found) {
          scan(true);
        }
          }
        };

        scan(false);
        if (!found) {
          scan(true);
        }

        if (found) {
          cursorFile = Chess::BoardState::fileOf(bestSq);
          cursorRank = Chess::BoardState::rankOf(bestSq);
          moved = true;

          // Keep index in sync for non-directional consumers
          for (size_t i = 0; i < navigablePieces.size(); i++) {
            if (navigablePieces[i] == bestSq) {
              navigablePieceIndex = static_cast<int>(i);
              break;
            }
          }
        }
      }
    }
  } else {
    // Navigate between legal move destinations (screen-space grid navigation)
    if (!legalMovesFromSelected.empty()) {
      const bool goUp = input_.wasPressed(HalGPIO::BTN_UP);
      const bool goDown = input_.wasPressed(HalGPIO::BTN_DOWN);
      const bool goLeft = input_.wasPressed(HalGPIO::BTN_LEFT);
      const bool goRight = input_.wasPressed(HalGPIO::BTN_RIGHT);

      if (goUp || goDown || goLeft || goRight) {
        const int curSq = cursorSquare();
        const int curFile = Chess::BoardState::fileOf(curSq);
        const int curRank = Chess::BoardState::rankOf(curSq);
        const int curX = screenX(curFile) + SQUARE_SIZE / 2;
        const int curY = screenY(curRank) + SQUARE_SIZE / 2;

        int bestSq = -1;
        int bestPrimary = 0;
        int bestSecondary = 0;
        bool found = false;

        auto consider = [&](int sq, int primary, int secondary) {
          if (!found || primary < bestPrimary || (primary == bestPrimary && secondary < bestSecondary) ||
              (primary == bestPrimary && secondary == bestSecondary && sq < bestSq)) {
            bestSq = sq;
            bestPrimary = primary;
            bestSecondary = secondary;
            found = true;
          }
        };

        auto scan = [&](bool wrap, int forcedUp = 0, int forcedDown = 0, int forcedLeft = 0, int forcedRight = 0) {
          found = false;

          const bool up = forcedUp ? (forcedUp > 0) : goUp;
          const bool down = forcedDown ? (forcedDown > 0) : goDown;
          const bool left = forcedLeft ? (forcedLeft > 0) : goLeft;
          const bool right = forcedRight ? (forcedRight > 0) : goRight;

          for (size_t i = 0; i < legalMovesFromSelected.size(); i++) {
            const int sq = legalMovesFromSelected[i].to;
            if (sq == curSq) continue;
            const int f = Chess::BoardState::fileOf(sq);
            const int r = Chess::BoardState::rankOf(sq);
            const int x = screenX(f) + SQUARE_SIZE / 2;
            const int y = screenY(r) + SQUARE_SIZE / 2;
            const int dx = x - curX;
            const int dy = y - curY;

            if (up) {
              if (!wrap && dy >= 0) continue;
              if (wrap && dy <= 0) continue;
              consider(sq, -dy, dx < 0 ? -dx : dx);
            } else if (down) {
              if (!wrap && dy <= 0) continue;
              if (wrap && dy >= 0) continue;
              consider(sq, dy, dx < 0 ? -dx : dx);
            } else if (left) {
              if (!wrap && dx >= 0) continue;
              if (wrap && dx <= 0) continue;
              consider(sq, -dx, dy < 0 ? -dy : dy);
            } else if (right) {
              if (!wrap && dx <= 0) continue;
              if (wrap && dx >= 0) continue;
              consider(sq, dx, dy < 0 ? -dy : dy);
            }
          }
        };

        scan(false);
        if (!found) {
          if (goLeft) {
            logEvent("NAV", "Wrapping LEFT->UP");
            scan(false, 1, 0, 0, 0);
          } else if (goRight) {
            logEvent("NAV", "Wrapping RIGHT->DOWN");
            scan(false, 0, 1, 0, 0);
          } else if (goUp) {
            logEvent("NAV", "Wrapping UP->LEFT");
            scan(false, 0, 0, 1, 0);
          } else if (goDown) {
            logEvent("NAV", "Wrapping DOWN->RIGHT");
            scan(false, 0, 0, 0, 1);
          }
        }
        if (!found) {
          scan(true);
        }

        if (found) {
          cursorFile = Chess::BoardState::fileOf(bestSq);
          cursorRank = Chess::BoardState::rankOf(bestSq);
          moved = true;

          for (size_t i = 0; i < legalMovesFromSelected.size(); i++) {
            if (legalMovesFromSelected[i].to == bestSq) {
              legalMoveNavIndex = static_cast<int>(i);
              break;
            }
          }
        }
      }
    }
  }

  if (input_.wasReleased(HalGPIO::BTN_CONFIRM)) {
    logBtn("CONFIRM");
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
    logBtn("BACK");
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
  
  if (currentMode == Mode::MainMenu) {
    renderMainMenu();
  } else if (currentMode == Mode::PackSelect) {
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
    renderHint();
    renderCursor();
    renderStatus();
    
    const char* btn2Label = "Select";
    if (puzzleSolved) {
      btn2Label = "Next";
    } else if (puzzleFailed) {
      btn2Label = "Retry";
    }
    renderer.drawButtonHints(UI_10_FONT_ID, "Menu (hold)", btn2Label, "<", ">");

    // Long-press indicator above the Menu button.
    {
      const int screenHeight = renderer.getScreenHeight();
      constexpr int buttonX = 25;
      constexpr int buttonWidth = 106;
      constexpr int buttonYFromBottom = 40;
      const int buttonTopY = screenHeight - buttonYFromBottom;
      const int cx = buttonX + buttonWidth / 2;
      const int cy = buttonTopY - 8;
      renderer.drawLine(cx - 6, cy - 4, cx, cy, true);
      renderer.drawLine(cx + 6, cy - 4, cx, cy, true);
    }
  }
  
  if (pendingFullRefresh) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pendingFullRefresh = false;
  } else {
    renderer.displayBuffer();
  }
}

void ChessPuzzlesApp::renderHint() {
  auto& renderer = renderer_;

  if (!hintActive) return;
  if (puzzleSolved || puzzleFailed) return;
  if (currentMoveIndex < 0 || currentMoveIndex >= static_cast<int>(currentPuzzle.solution.size())) return;

  const Chess::Move& m = currentPuzzle.solution[currentMoveIndex];

  auto drawBox = [&](int sq, int thickness, bool isToSquare) {
    const int file = Chess::BoardState::fileOf(sq);
    const int rank = Chess::BoardState::rankOf(sq);
    const int x = screenX(file);
    const int y = screenY(rank);

    const bool squareIsLight = (file + rank) % 2 == 1;
    const bool color = squareIsLight;

    if (isToSquare) {
      drawHatchedRect(x, y, SQUARE_SIZE, SQUARE_SIZE, color, 4);
      return;
    }

    drawHatchedRect(x, y, SQUARE_SIZE, SQUARE_SIZE, color, 6);

    constexpr int cornerLen = 18;
    for (int t = 0; t < thickness; t++) {
      renderer.drawLine(x + t, y, x + t, y + cornerLen, color);
      renderer.drawLine(x, y + t, x + cornerLen, y + t, color);

      renderer.drawLine(x + SQUARE_SIZE - 1 - t, y, x + SQUARE_SIZE - 1 - t, y + cornerLen, color);
      renderer.drawLine(x + SQUARE_SIZE - cornerLen, y + t, x + SQUARE_SIZE - 1, y + t, color);

      renderer.drawLine(x + t, y + SQUARE_SIZE - cornerLen, x + t, y + SQUARE_SIZE - 1, color);
      renderer.drawLine(x, y + SQUARE_SIZE - 1 - t, x + cornerLen, y + SQUARE_SIZE - 1 - t, color);

      renderer.drawLine(x + SQUARE_SIZE - 1 - t, y + SQUARE_SIZE - cornerLen,
                        x + SQUARE_SIZE - 1 - t, y + SQUARE_SIZE - 1, color);
      renderer.drawLine(x + SQUARE_SIZE - cornerLen, y + SQUARE_SIZE - 1 - t,
                        x + SQUARE_SIZE - 1, y + SQUARE_SIZE - 1 - t, color);
    }
  };

  drawBox(m.from, 4, false);
  drawBox(m.to, 3, true);
}

void ChessPuzzlesApp::renderInGameMenu() {
  auto& renderer = renderer_;

  // Keep the board visible, but render the menu in the blank space below the board.
  renderBoard();
  renderLegalMoveHints();
  renderCursor();

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  // drawButtonHints() uses a fixed 40px band at the bottom.
  constexpr int hintBandHeight = 40;
  constexpr int margin = 10;

  const int maxPanelWidth = (screenWidth - margin * 2);
  const int boardWidth = (BOARD_SIZE < screenWidth) ? BOARD_SIZE : screenWidth;
  const int desiredPanelWidth = boardWidth - margin * 2;
  const int panelWidth = (desiredPanelWidth < maxPanelWidth) ? desiredPanelWidth : maxPanelWidth;
  const int panelX = (screenWidth > BOARD_SIZE) ? (BOARD_OFFSET_X + (BOARD_SIZE - panelWidth) / 2)
                                                : ((screenWidth - panelWidth) / 2);

  const int blankTop = STATUS_Y;
  const int blankBottom = screenHeight - hintBandHeight - margin;
  const int availableHeight = blankBottom - blankTop;
  constexpr int itemLineHeight = 32;
  const int desiredHeight = 60 + (IN_GAME_MENU_ITEM_COUNT * itemLineHeight) + 46;
  const int panelHeight = (desiredHeight < availableHeight) ? desiredHeight : availableHeight;
  const int panelY = blankTop + (availableHeight - panelHeight) / 2;

  // Solid background so text stays readable on top of the board.
  renderer.fillRect(panelX, panelY, panelWidth, panelHeight);

  const char* title = "Puzzle Menu";
  const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, title);
  renderer.drawText(UI_12_FONT_ID, (screenWidth - titleWidth) / 2, panelY + 18, title, false);

  const char* items[] = {"Retry", "Skip", "Hint", "Refresh", "Exit"};
  constexpr int itemCount = IN_GAME_MENU_ITEM_COUNT;

  const int itemStartY = panelY + 60;
  const int itemTextX = panelX + 26;

  for (int i = 0; i < itemCount; i++) {
    const int y = itemStartY + i * itemLineHeight;

        if (i == inGameMenuIndex) {
      const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, items[i]);
      const int rectX = (screenWidth - textWidth) / 2 - MENU_HIGHLIGHT_PADDING;
      renderer.fillRect(rectX, y - 6, textWidth + MENU_HIGHLIGHT_PADDING * 2, itemLineHeight - 6);
      renderer.drawText(UI_12_FONT_ID, rectX + MENU_HIGHLIGHT_PADDING, y, items[i], false);
    } else {
      renderer.drawText(UI_12_FONT_ID, itemTextX, y, items[i], false);
    }
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

void ChessPuzzlesApp::drawHatchedRect(int x, int y, int w, int h, bool color, int spacing) {
  for (int i = -h; i < w; i += spacing) {
    int x0 = x + i;
    int y0 = y;
    int x1 = x + i + h;
    int y1 = y + h;

    if (x0 < x) {
      y0 += (x - x0);
      x0 = x;
    }
    if (x1 > x + w) {
      y1 -= (x1 - (x + w));
      x1 = x + w;
    }
    if (y0 < y) {
      x0 += (y - y0);
      y0 = y;
    }
    if (y1 > y + h) {
      x1 -= (y1 - (y + h));
      y1 = y + h;
    }

    if (x0 < x1 && y0 < y1) {
      renderer_.drawLine(x0, y0, x1, y1, color);
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
    
    // Keep the status block compact; the blank area is limited.
    {
      std::string pack = packName.empty() ? "(no pack)" : packName;
      const int idx = static_cast<int>(currentPuzzleIndex) + 1;
      const int total = static_cast<int>(puzzleCount);

      char line2[96];
      snprintf(line2, sizeof(line2), "%s  %d/%d  r%d", pack.c_str(), idx, total, currentPuzzle.rating);
      renderer.drawCenteredText(UI_10_FONT_ID, y + 25, line2);

      int infoY = y + 50;
      if (board.inCheck()) {
        renderer.drawCenteredText(UI_10_FONT_ID, infoY, "Check!");
        infoY += 20;
      }

      if (!activeTheme.empty()) {
        std::string prettyTheme = activeTheme;
        std::replace(prettyTheme.begin(), prettyTheme.end(), '_', ' ');
        std::string themeLine = \"Theme: \" + prettyTheme;
        const int maxW = renderer.getScreenWidth() - 20;
        if (renderer.getTextWidth(UI_10_FONT_ID, themeLine.c_str()) > maxW) {
          themeLine = renderer.truncatedText(UI_10_FONT_ID, themeLine.c_str(), maxW);
        }
        renderer.drawCenteredText(UI_10_FONT_ID, infoY, themeLine.c_str());
        infoY += 20;
      }

      if (!currentPuzzle.opening.empty()) {
        std::string openingLine = \"Opening: \" + currentPuzzle.opening;
        const int maxW = renderer.getScreenWidth() - 20;
        if (renderer.getTextWidth(UI_10_FONT_ID, openingLine.c_str()) > maxW) {
          openingLine = renderer.truncatedText(UI_10_FONT_ID, openingLine.c_str(), maxW);
        }
        renderer.drawCenteredText(UI_10_FONT_ID, infoY, openingLine.c_str());
        infoY += 20;
      }

      renderer.drawCenteredText(UI_10_FONT_ID, infoY, \"Hold Menu for options\");
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

  renderer.drawCenteredText(UI_12_FONT_ID, 30, "Chess");
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
        int rectX = (screenWidth - textWidth) / 2 - MENU_HIGHLIGHT_PADDING;
        renderer.fillRect(rectX, y - 2, textWidth + MENU_HIGHLIGHT_PADDING * 2, lineHeight - 4);
        renderer.drawText(UI_12_FONT_ID, rectX + MENU_HIGHLIGHT_PADDING, y, displayName.c_str(), false);
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
  // Allow pack files to evolve record size while keeping backward compatibility.
  packRecordSize = packHeader.recordSize;
  if (packRecordSize < Chess::RECORD_SIZE || packRecordSize > 1024) {
    Serial.printf("[CHESS] Invalid record size %d; using default %d\n", packRecordSize, Chess::RECORD_SIZE);
    packRecordSize = Chess::RECORD_SIZE;
  }
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
  
  uint32_t offset = Chess::PACK_HEADER_SIZE + index * packRecordSize;
  if (!file.seek(offset)) {
    file.close();
    return false;
  }
  
  std::vector<uint8_t> record;
  record.resize(packRecordSize);
  if (file.read(record.data(), packRecordSize) != packRecordSize) {
    file.close();
    return false;
  }
  file.close();
  
  currentPuzzle = Chess::Puzzle::fromRecord(record.data(), packRecordSize);
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
  currentPuzzle.themes.clear();
  currentPuzzle.opening.clear();
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
  // Hint is single-use: once the user attempts a move, clear it.
  hintActive = false;

  if (currentMoveIndex >= static_cast<int>(currentPuzzle.solution.size())) {
    logEvent("MOVE", "attempt=%d->%d unexpected=end_of_solution", move.from, move.to);
    onPuzzleFailed();
    return;
  }
  
  const Chess::Move& expectedMove = currentPuzzle.solution[currentMoveIndex];

  if (move.from != expectedMove.from || move.to != expectedMove.to) {
    logEvent("MOVE", "attempt=%d->%d expected=%d->%d result=mismatch", move.from, move.to,
             expectedMove.from, expectedMove.to);
    onPuzzleFailed();
    return;
  }

  if (!tryMove(move)) {
    logEvent("MOVE", "attempt=%d->%d expected=%d->%d result=illegal", move.from, move.to,
             expectedMove.from, expectedMove.to);
    onPuzzleFailed();
    return;
  }

  logEvent("MOVE", "attempt=%d->%d expected=%d->%d result=ok", move.from, move.to,
           expectedMove.from, expectedMove.to);
  
  deselectPiece();
  currentMoveIndex++;
  
  movesSinceFullRefresh++;
  if (movesSinceFullRefresh >= 10) {
    pendingFullRefresh = true;
    movesSinceFullRefresh = 0;
  }
  
  if (currentMoveIndex >= static_cast<int>(currentPuzzle.solution.size())) {
    logEvent("PUZZLE", "solved=1 index=%lu", static_cast<unsigned long>(currentPuzzleIndex));
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
  return "/.crosspoint/chess/progress/" + packName + ".bin";
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
    // Backward compatibility: older versions stored progress at the root chess folder.
    std::string legacyPath = "/.crosspoint/chess/progress_";
    {
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
      legacyPath += packName;
    }
    legacyPath += ".bin";

    if (!SdMan.openFileForRead("CHESS", legacyPath, file)) {
      Serial.printf("[CHESS] No saved progress found at %s\n", progressPath.c_str());
      return 0;
    }

    Serial.printf("[CHESS] Loaded legacy progress from %s\n", legacyPath.c_str());
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
      int textWidth = renderer.getTextWidth(UI_12_FONT_ID, menuItems[i]);
      int rectX = (screenWidth - textWidth) / 2 - MENU_HIGHLIGHT_PADDING;
      renderer.fillRect(rectX, y - 2, textWidth + MENU_HIGHLIGHT_PADDING * 2, lineHeight - 8);
      renderer.drawText(UI_12_FONT_ID, rectX + MENU_HIGHLIGHT_PADDING, y, menuItems[i], false);
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
      int textWidth = renderer.getTextWidth(UI_12_FONT_ID, itemStr);
      int rectX = listX + 10 - MENU_HIGHLIGHT_PADDING;
      renderer.fillRect(rectX, y - 2, textWidth + MENU_HIGHLIGHT_PADDING * 2, lineHeight - 4);
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
    int screenWidth = renderer.getScreenWidth();
    const int itemWidth = (screenWidth - 80) < 360 ? (screenWidth - 80) : 360;
    int listX = (screenWidth - itemWidth) / 2;
    
    int startIdx = 0;
    if (themeSelectIndex >= maxVisible) {
      startIdx = themeSelectIndex - maxVisible + 1;
    }
    
    for (int i = 0; i < maxVisible && (startIdx + i) < static_cast<int>(availableThemes.size()); i++) {
      int idx = startIdx + i;
      int y = startY + i * lineHeight;
      
      const std::string& theme = availableThemes[idx];
      std::string prettyTheme = theme;
      std::replace(prettyTheme.begin(), prettyTheme.end(), '_', ' ');
      
      if (idx == themeSelectIndex) {
        int textWidth = renderer.getTextWidth(UI_10_FONT_ID, prettyTheme.c_str());
        int rectX = listX + 10 - MENU_HIGHLIGHT_PADDING;
        renderer.fillRect(rectX, y - 2, textWidth + MENU_HIGHLIGHT_PADDING * 2, lineHeight - 4);
        renderer.drawText(UI_10_FONT_ID, listX + 10, y, prettyTheme.c_str(), false);
      } else {
        renderer.drawText(UI_10_FONT_ID, listX + 10, y, prettyTheme.c_str(), true);
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

void ChessPuzzlesApp::renderPartitionError() {
  auto& renderer = renderer_;
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 160, "Cannot return to launcher");
  renderer.drawCenteredText(UI_10_FONT_ID, 200, "Target partition invalid");
  renderer.drawButtonHints(UI_10_FONT_ID, "Exit", "", "", "");
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

bool ChessPuzzlesApp::validatePartition(const esp_partition_t* partition) {
  if (!partition) {
    Serial.println("[CHESS] validatePartition: partition is null");
    return false;
  }

  uint8_t magic;
  esp_err_t err = esp_partition_read(partition, 0, &magic, 1);
  if (err != ESP_OK) {
    Serial.printf("[CHESS] validatePartition: failed to read partition %s, err=%d\n", partition->label, err);
    return false;
  }

  Serial.printf("[CHESS] validatePartition: partition %s magic=0x%02X\n", partition->label, magic);
  return (magic == 0xE9);
}

void ChessPuzzlesApp::returnToLauncher() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* target = nullptr;

  Serial.printf("[CHESS] [%lu] NAV: Returning to launcher\n", millis());
  esp_partition_subtype_t runningSubtype = running ? running->subtype : static_cast<esp_partition_subtype_t>(0);
  esp_partition_subtype_t targetSubtype = static_cast<esp_partition_subtype_t>(0);

  if (runningSubtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
    targetSubtype = ESP_PARTITION_SUBTYPE_APP_OTA_1;
    target = esp_partition_find_first(ESP_PARTITION_TYPE_APP, targetSubtype, nullptr);
  } else if (runningSubtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
    targetSubtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
    target = esp_partition_find_first(ESP_PARTITION_TYPE_APP, targetSubtype, nullptr);
  } else {
    Serial.printf("[CHESS] [%lu] BOOT: Running partition subtype not ota_0/ota_1 (%d); falling back to next update partition\n", 
                  millis(), static_cast<int>(runningSubtype));
  }

  if (!target) {
    if (runningSubtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 || runningSubtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
      Serial.printf("[CHESS] [%lu] BOOT: Target OTA partition not found for subtype=%d; falling back to next update partition\n", 
                    millis(), static_cast<int>(targetSubtype));
    }
    // Explicitly pass running to avoid ambiguous NULL behavior.
    target = esp_ota_get_next_update_partition(running);
  }

  Serial.printf("[CHESS] [%lu] BOOT: Running partition label=%s subtype=%d\n", 
                millis(), running ? running->label : "<null>", static_cast<int>(runningSubtype));
  Serial.printf("[CHESS] [%lu] BOOT: Target partition label=%s subtype=%d addr=0x%08X\n", 
                millis(), target ? target->label : "<null>", target ? static_cast<int>(target->subtype) : -1,
                target ? target->address : 0);

  if (!validatePartition(target)) {
    Serial.printf("[CHESS] [%lu] ERROR: Aborting returnToLauncher: target partition validation failed\n", millis());
    renderPartitionError();
    return;
  }

  esp_err_t err = target ? esp_ota_set_boot_partition(target) : ESP_ERR_NOT_FOUND;
  Serial.printf("[CHESS] [%lu] BOOT: esp_ota_set_boot_partition result=%d\n", millis(), static_cast<int>(err));

  // If we failed to set the boot partition, restarting will likely return to this app.
  // Still restart so the user isn't trapped, but log clearly for debugging.
  if (err != ESP_OK) {
    Serial.printf("[CHESS] [%lu] ERROR: failed to switch boot partition; restart may relaunch this app\n", millis());
  }

  delay(50);
  esp_restart();
}
