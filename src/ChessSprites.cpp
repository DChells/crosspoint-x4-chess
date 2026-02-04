#include "ChessSprites.h"
#include "EmbeddedChessSprites.h"
#include <Arduino.h>
#include <SDCardManager.h>
#include <cstring>

namespace ChessSprites {

static const uint8_t* spriteData[12] = {nullptr};
static uint8_t* spriteOverrides[12] = {nullptr};
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

  // Default to embedded sprites (always available)
  for (int i = 0; i < 12; i++) {
    spriteData[i] = EmbeddedChessSprites::SPRITES[i];
    spriteOverrides[i] = nullptr;
  }

  Serial.println("[CHESS] Loaded embedded sprites");

  int overridesLoaded = 0;

  for (int i = 0; i < 12; i++) {
    FsFile file;
    if (!SdMan.openFileForRead("CHESS", SPRITE_FILES[i], file)) {
      continue;
    }

    if (file.size() != PIECE_BYTES) {
      Serial.printf("[CHESS] Invalid sprite size (using embedded): %s (%d bytes, expected %d)\n",
                    SPRITE_FILES[i], (int)file.size(), PIECE_BYTES);
      file.close();
      continue;
    }

    uint8_t* buf = (uint8_t*)malloc(PIECE_BYTES);
    if (!buf) {
      Serial.printf("[CHESS] Failed to allocate override sprite %d (using embedded)\n", i + 1);
      file.close();
      continue;
    }

    size_t bytesRead = file.read(buf, PIECE_BYTES);
    file.close();

    if (bytesRead != PIECE_BYTES) {
      Serial.printf("[CHESS] Failed to read sprite (using embedded): %s (expected %d, got %d)\n",
                    SPRITE_FILES[i], PIECE_BYTES, bytesRead);
      free(buf);
      continue;
    }

    spriteOverrides[i] = buf;
    spriteData[i] = spriteOverrides[i];
    overridesLoaded++;
  }

  spritesLoaded = true;
  Serial.printf("[CHESS] Loaded sprites: embedded=12 overrides=%d\n", overridesLoaded);
  return true;
}

void freeSprites() {
  for (int i = 0; i < 12; i++) {
    if (spriteOverrides[i]) {
      free(spriteOverrides[i]);
      spriteOverrides[i] = nullptr;
    }
    spriteData[i] = EmbeddedChessSprites::SPRITES[i];
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
