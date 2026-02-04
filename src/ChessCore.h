#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Chess {

// ============================================================================
// Piece representation
// ============================================================================

enum Piece : int8_t {
    NONE = 0,
    W_PAWN = 1, W_KNIGHT = 2, W_BISHOP = 3, W_ROOK = 4, W_QUEEN = 5, W_KING = 6,
    B_PAWN = 7, B_KNIGHT = 8, B_BISHOP = 9, B_ROOK = 10, B_QUEEN = 11, B_KING = 12
};

inline bool isWhite(Piece p) { return p >= W_PAWN && p <= W_KING; }
inline bool isBlack(Piece p) { return p >= B_PAWN && p <= B_KING; }
inline bool isEmpty(Piece p) { return p == NONE; }

// Get piece type (1-6) regardless of color
inline int pieceType(Piece p) {
    if (p == NONE) return 0;
    return isWhite(p) ? p : (p - 6);
}

// ============================================================================
// Move representation
// ============================================================================

// Compact move: from(6) | to(6) | promo(4) packed into 16 bits
// Promo: 0=none, 1=knight, 2=bishop, 3=rook, 4=queen
struct Move {
    uint8_t from;
    uint8_t to;
    uint8_t promo;  // 0=none, 1=N, 2=B, 3=R, 4=Q
    
    Move() : from(0), to(0), promo(0) {}
    Move(uint8_t f, uint8_t t, uint8_t p = 0) : from(f), to(t), promo(p) {}
    
    bool operator==(const Move& other) const {
        return from == other.from && to == other.to && promo == other.promo;
    }
    
    bool isNull() const { return from == to; }
    
    // Pack to 16-bit (for storage)
    uint16_t pack() const {
        return (from & 0x3F) | ((to & 0x3F) << 6) | ((promo & 0x0F) << 12);
    }
    
    // Unpack from 16-bit
    static Move unpack(uint16_t val) {
        return Move(val & 0x3F, (val >> 6) & 0x3F, (val >> 12) & 0x0F);
    }
};

// ============================================================================
// Board state
// ============================================================================

struct BoardState {
    Piece board[64];       // a1=0, h1=7, a8=56, h8=63
    bool whiteToMove;
    uint8_t castling;      // bit 0: K, bit 1: Q, bit 2: k, bit 3: q
    int8_t epSquare;       // -1 if none, otherwise the en passant target square
    uint8_t halfmoveClock; // For 50-move rule (optional)
    uint16_t fullmoveNum;  // Move number (optional)
    
    BoardState();
    
    // Square helpers
    static int fileOf(int sq) { return sq & 7; }
    static int rankOf(int sq) { return sq >> 3; }
    static int makeSquare(int file, int rank) { return rank * 8 + file; }
    static bool isValidSquare(int sq) { return sq >= 0 && sq < 64; }
    
    // Piece access
    Piece at(int sq) const { return board[sq]; }
    void set(int sq, Piece p) { board[sq] = p; }
    
    // Find king
    int findKing(bool white) const;
    
    // Check if a square is attacked by the given side
    bool isAttacked(int sq, bool byWhite) const;
    
    // Check if the side to move is in check
    bool inCheck() const;
    
    // Generate all legal moves for the side to move
    std::vector<Move> generateLegalMoves() const;
    
    // Generate legal moves for a specific piece at a square
    std::vector<Move> generateLegalMovesFrom(int sq) const;
    
    // Check if a move is legal (validates and checks for leaving king in check)
    bool isLegalMove(const Move& move) const;
    
    // Apply a move and return the new state
    BoardState applyMove(const Move& move) const;
    
    // Check for checkmate or stalemate
    bool isCheckmate() const;
    bool isStalemate() const;
    
    // Parse from packed binary (matches packer format)
    static BoardState fromPacked(const uint8_t* data);
    
private:
    // Generate pseudo-legal moves (may leave king in check)
    std::vector<Move> generatePseudoLegalMoves() const;
    std::vector<Move> generatePseudoLegalMovesFrom(int sq) const;
    
    // Sliding piece move generation
    void generateSlidingMoves(int sq, const int* directions, int numDirs, 
                              std::vector<Move>& moves) const;
    
    // Pawn move generation
    void generatePawnMoves(int sq, std::vector<Move>& moves) const;
    
    // Knight move generation
    void generateKnightMoves(int sq, std::vector<Move>& moves) const;
    
    // King move generation (including castling)
    void generateKingMoves(int sq, std::vector<Move>& moves) const;
};

// ============================================================================
// Puzzle data
// ============================================================================

struct Puzzle {
    uint16_t rating;
    BoardState position;
    std::vector<Move> solution;
    std::string themes;
    std::string opening;
    
    static Puzzle fromRecord(const uint8_t* data, uint16_t recordSize);
};

struct PackHeader {
    uint16_t recordSize;
    uint32_t puzzleCount;
    uint16_t ratingMin;
    uint16_t ratingMax;
    
    static bool fromFile(const uint8_t* headerData, PackHeader& out);
};

static constexpr int RECORD_SIZE = 96;
static constexpr int PACK_HEADER_SIZE = 18;

}  // namespace Chess
