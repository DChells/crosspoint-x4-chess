#pragma once

#include <cstdint>

namespace ChessSprites {

constexpr int PIECE_SIZE = 60;
constexpr int PIECE_BYTES = (PIECE_SIZE * PIECE_SIZE + 7) / 8;

bool loadSprites();
void freeSprites();
const uint8_t* getPieceSprite(int piece);

}
