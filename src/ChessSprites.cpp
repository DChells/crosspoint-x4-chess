#include "ChessSprites.h"
#include <Arduino.h>
#include <SDCardManager.h>
#include <cstring>

namespace ChessSprites {

static uint8_t* spriteData[12] = {nullptr};
static bool spritesLoaded = false;

static const char* SPRITE_FILES[12] = {
  "/.crosspoint/chess/sprites/01_pawn_outline.bin",
  "/.crosspoint/chess/sprites/02_knight_outline.bin",
  "/.crosspoint/chess/sprites/03_bishop_outline.bin",
  "/.crosspoint/chess/sprites/04_rook_outline.bin",
  "/.crosspoint/chess/sprites/05_queen_outline.bin",
  "/.crosspoint/chess/sprites/06_king_outline.bin",
  "/.crosspoint/chess/sprites/07_pawn_filled.bin",
  "/.crosspoint/chess/sprites/08_knight_filled.bin",
  "/.crosspoint/chess/sprites/09_bishop_filled.bin",
  "/.crosspoint/chess/sprites/10_rook_filled.bin",
  "/.crosspoint/chess/sprites/11_queen_filled.bin",
  "/.crosspoint/chess/sprites/12_king_filled.bin"
};

bool loadSprites() {
  if (spritesLoaded) {
    return true;
  }

  SdMan.mkdir("/.crosspoint/chess/sprites");

  for (int i = 0; i < 12; i++) {
    spriteData[i] = (uint8_t*)malloc(PIECE_BYTES);
    if (!spriteData[i]) {
      Serial.printf("[CHESS] Failed to allocate memory for sprite %d\n", i + 1);
      freeSprites();
      return false;
    }

    FsFile file;
    if (!SdMan.openFileForRead("CHESS", SPRITE_FILES[i], file)) {
      Serial.printf("[CHESS] Failed to open sprite: %s\n", SPRITE_FILES[i]);
      freeSprites();
      return false;
    }

    if (file.size() != PIECE_BYTES) {
      Serial.printf("[CHESS] Invalid sprite size: %s (%d bytes, expected %d)\n",
                   SPRITE_FILES[i], (int)file.size(), PIECE_BYTES);
      file.close();
      freeSprites();
      return false;
    }

    size_t bytesRead = file.read(spriteData[i], PIECE_BYTES);
    file.close();

    if (bytesRead != PIECE_BYTES) {
      Serial.printf("[CHESS] Failed to read sprite: %s (expected %d, got %d)\n",
                   SPRITE_FILES[i], PIECE_BYTES, bytesRead);
      freeSprites();
      return false;
    }
  }

  spritesLoaded = true;
  Serial.printf("[CHESS] Loaded %d sprites (%d bytes)\n", 12, 12 * PIECE_BYTES);
  return true;
}

void freeSprites() {
  for (int i = 0; i < 12; i++) {
    if (spriteData[i]) {
      free(spriteData[i]);
      spriteData[i] = nullptr;
    }
  }
  if (spritesLoaded) {
    spritesLoaded = false;
    Serial.println("[CHESS] Freed sprite memory");
  }
}

const uint8_t* getPieceSprite(int piece) {
  if (!spritesLoaded) {
    return nullptr;
  }

  if (piece < 1 || piece > 12) {
    return nullptr;
  }

  return spriteData[piece - 1];
}

}
