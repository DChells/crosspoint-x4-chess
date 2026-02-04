#pragma once

#include <cstdint>

namespace EmbeddedChessSprites {

constexpr int PIECE_SIZE = 60;
constexpr int PIECE_BYTES = (PIECE_SIZE * PIECE_SIZE + 7) / 8;

extern const uint8_t SPRITES[12][PIECE_BYTES];

}  // namespace EmbeddedChessSprites
