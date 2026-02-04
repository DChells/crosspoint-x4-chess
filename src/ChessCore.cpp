#include "ChessCore.h"
#include <cstring>
#include <algorithm>

namespace Chess {

namespace {
    const int KNIGHT_OFFSETS[] = {-17, -15, -10, -6, 6, 10, 15, 17};
    const int KING_OFFSETS[] = {-9, -8, -7, -1, 1, 7, 8, 9};
    const int BISHOP_DIRS[] = {-9, -7, 7, 9};
    const int ROOK_DIRS[] = {-8, -1, 1, 8};
    const int QUEEN_DIRS[] = {-9, -8, -7, -1, 1, 7, 8, 9};

    std::string readRecordField(const uint8_t* data, size_t offset, size_t maxLen) {
        const char* start = reinterpret_cast<const char*>(data + offset);
        size_t len = 0;
        while (len < maxLen && start[len] != '\0') {
            ++len;
        }
        return std::string(start, len);
    }
}

BoardState::BoardState() {
    memset(board, NONE, sizeof(board));
    whiteToMove = true;
    castling = 0;
    epSquare = -1;
    halfmoveClock = 0;
    fullmoveNum = 1;
}

int BoardState::findKing(bool white) const {
    Piece target = white ? W_KING : B_KING;
    for (int sq = 0; sq < 64; sq++) {
        if (board[sq] == target) return sq;
    }
    return -1;
}

bool BoardState::isAttacked(int sq, bool byWhite) const {
    int file = fileOf(sq);
    int rank = rankOf(sq);
    
    Piece enemyPawn = byWhite ? W_PAWN : B_PAWN;
    Piece enemyKnight = byWhite ? W_KNIGHT : B_KNIGHT;
    Piece enemyBishop = byWhite ? W_BISHOP : B_BISHOP;
    Piece enemyRook = byWhite ? W_ROOK : B_ROOK;
    Piece enemyQueen = byWhite ? W_QUEEN : B_QUEEN;
    Piece enemyKing = byWhite ? W_KING : B_KING;
    
    int pawnDir = byWhite ? -1 : 1;
    int pawnRank = rank + pawnDir;
    if (pawnRank >= 0 && pawnRank < 8) {
        if (file > 0) {
            int pawnSq = makeSquare(file - 1, pawnRank);
            if (board[pawnSq] == enemyPawn) return true;
        }
        if (file < 7) {
            int pawnSq = makeSquare(file + 1, pawnRank);
            if (board[pawnSq] == enemyPawn) return true;
        }
    }
    
    for (int offset : KNIGHT_OFFSETS) {
        int target = sq + offset;
        if (!isValidSquare(target)) continue;
        int tf = fileOf(target);
        int fileDiff = abs(file - tf);
        if (fileDiff > 2) continue;
        if (board[target] == enemyKnight) return true;
    }
    
    for (int offset : KING_OFFSETS) {
        int target = sq + offset;
        if (!isValidSquare(target)) continue;
        int tf = fileOf(target);
        int fileDiff = abs(file - tf);
        if (fileDiff > 1) continue;
        if (board[target] == enemyKing) return true;
    }
    
    for (int dir : BISHOP_DIRS) {
        int current = sq;
        while (true) {
            int prevFile = fileOf(current);
            current += dir;
            if (!isValidSquare(current)) break;
            int newFile = fileOf(current);
            if (abs(newFile - prevFile) != 1) break;
            
            Piece p = board[current];
            if (p == enemyBishop || p == enemyQueen) return true;
            if (p != NONE) break;
        }
    }
    
    for (int dir : ROOK_DIRS) {
        int current = sq;
        while (true) {
            int prevFile = fileOf(current);
            current += dir;
            if (!isValidSquare(current)) break;
            int newFile = fileOf(current);
            if (abs(dir) == 1 && newFile != prevFile + dir) break;
            if (abs(dir) == 8 && newFile != prevFile) break;
            
            Piece p = board[current];
            if (p == enemyRook || p == enemyQueen) return true;
            if (p != NONE) break;
        }
    }
    
    return false;
}

bool BoardState::inCheck() const {
    int kingSq = findKing(whiteToMove);
    if (kingSq < 0) return false;
    return isAttacked(kingSq, !whiteToMove);
}

void BoardState::generatePawnMoves(int sq, std::vector<Move>& moves) const {
    int file = fileOf(sq);
    int rank = rankOf(sq);
    bool isWhite = Chess::isWhite(board[sq]);
    int dir = isWhite ? 1 : -1;
    int startRank = isWhite ? 1 : 6;
    int promoRank = isWhite ? 7 : 0;
    
    int forward = sq + dir * 8;
    if (isValidSquare(forward) && board[forward] == NONE) {
        if (rankOf(forward) == promoRank) {
            moves.push_back(Move(sq, forward, 4));
            moves.push_back(Move(sq, forward, 3));
            moves.push_back(Move(sq, forward, 2));
            moves.push_back(Move(sq, forward, 1));
        } else {
            moves.push_back(Move(sq, forward, 0));
            
            if (rank == startRank) {
                int doubleForward = sq + dir * 16;
                if (board[doubleForward] == NONE) {
                    moves.push_back(Move(sq, doubleForward, 0));
                }
            }
        }
    }
    
    int captureLeft = sq + dir * 8 - 1;
    int captureRight = sq + dir * 8 + 1;
    
    if (file > 0 && isValidSquare(captureLeft)) {
        Piece target = board[captureLeft];
        bool canCapture = (target != NONE && isWhite != Chess::isWhite(target));
        if (!canCapture && captureLeft == epSquare) canCapture = true;
        
        if (canCapture) {
            if (rankOf(captureLeft) == promoRank) {
                moves.push_back(Move(sq, captureLeft, 4));
                moves.push_back(Move(sq, captureLeft, 3));
                moves.push_back(Move(sq, captureLeft, 2));
                moves.push_back(Move(sq, captureLeft, 1));
            } else {
                moves.push_back(Move(sq, captureLeft, 0));
            }
        }
    }
    
    if (file < 7 && isValidSquare(captureRight)) {
        Piece target = board[captureRight];
        bool canCapture = (target != NONE && isWhite != Chess::isWhite(target));
        if (!canCapture && captureRight == epSquare) canCapture = true;
        
        if (canCapture) {
            if (rankOf(captureRight) == promoRank) {
                moves.push_back(Move(sq, captureRight, 4));
                moves.push_back(Move(sq, captureRight, 3));
                moves.push_back(Move(sq, captureRight, 2));
                moves.push_back(Move(sq, captureRight, 1));
            } else {
                moves.push_back(Move(sq, captureRight, 0));
            }
        }
    }
}

void BoardState::generateKnightMoves(int sq, std::vector<Move>& moves) const {
    int file = fileOf(sq);
    bool isWhite = Chess::isWhite(board[sq]);
    
    for (int offset : KNIGHT_OFFSETS) {
        int target = sq + offset;
        if (!isValidSquare(target)) continue;
        int tf = fileOf(target);
        int fileDiff = abs(file - tf);
        if (fileDiff > 2) continue;
        
        Piece p = board[target];
        if (p == NONE || (isWhite != Chess::isWhite(p))) {
            moves.push_back(Move(sq, target, 0));
        }
    }
}

void BoardState::generateSlidingMoves(int sq, const int* directions, int numDirs,
                                       std::vector<Move>& moves) const {
    int file = fileOf(sq);
    bool isWhite = Chess::isWhite(board[sq]);
    
    for (int i = 0; i < numDirs; i++) {
        int dir = directions[i];
        int current = sq;
        
        while (true) {
            int prevFile = fileOf(current);
            current += dir;
            if (!isValidSquare(current)) break;
            int newFile = fileOf(current);
            
            if (abs(dir) == 1) {
                if (newFile != prevFile + dir) break;
            } else if (abs(dir) == 7 || abs(dir) == 9) {
                if (abs(newFile - prevFile) != 1) break;
            }
            
            Piece p = board[current];
            if (p == NONE) {
                moves.push_back(Move(sq, current, 0));
            } else {
                if (isWhite != Chess::isWhite(p)) {
                    moves.push_back(Move(sq, current, 0));
                }
                break;
            }
        }
    }
}

void BoardState::generateKingMoves(int sq, std::vector<Move>& moves) const {
    int file = fileOf(sq);
    bool isWhite = Chess::isWhite(board[sq]);
    
    for (int offset : KING_OFFSETS) {
        int target = sq + offset;
        if (!isValidSquare(target)) continue;
        int tf = fileOf(target);
        int fileDiff = abs(file - tf);
        if (fileDiff > 1) continue;
        
        Piece p = board[target];
        if (p == NONE || (isWhite != Chess::isWhite(p))) {
            moves.push_back(Move(sq, target, 0));
        }
    }
    
    if (isWhite && sq == 4) {
        if ((castling & 1) && board[5] == NONE && board[6] == NONE && board[7] == W_ROOK) {
            if (!isAttacked(4, false) && !isAttacked(5, false) && !isAttacked(6, false)) {
                moves.push_back(Move(4, 6, 0));
            }
        }
        if ((castling & 2) && board[3] == NONE && board[2] == NONE && board[1] == NONE && board[0] == W_ROOK) {
            if (!isAttacked(4, false) && !isAttacked(3, false) && !isAttacked(2, false)) {
                moves.push_back(Move(4, 2, 0));
            }
        }
    } else if (!isWhite && sq == 60) {
        if ((castling & 4) && board[61] == NONE && board[62] == NONE && board[63] == B_ROOK) {
            if (!isAttacked(60, true) && !isAttacked(61, true) && !isAttacked(62, true)) {
                moves.push_back(Move(60, 62, 0));
            }
        }
        if ((castling & 8) && board[59] == NONE && board[58] == NONE && board[57] == NONE && board[56] == B_ROOK) {
            if (!isAttacked(60, true) && !isAttacked(59, true) && !isAttacked(58, true)) {
                moves.push_back(Move(60, 58, 0));
            }
        }
    }
}

std::vector<Move> BoardState::generatePseudoLegalMovesFrom(int sq) const {
    std::vector<Move> moves;
    Piece p = board[sq];
    
    if (p == NONE) return moves;
    if (whiteToMove != Chess::isWhite(p)) return moves;
    
    int type = pieceType(p);
    
    switch (type) {
        case 1: generatePawnMoves(sq, moves); break;
        case 2: generateKnightMoves(sq, moves); break;
        case 3: generateSlidingMoves(sq, BISHOP_DIRS, 4, moves); break;
        case 4: generateSlidingMoves(sq, ROOK_DIRS, 4, moves); break;
        case 5: generateSlidingMoves(sq, QUEEN_DIRS, 8, moves); break;
        case 6: generateKingMoves(sq, moves); break;
    }
    
    return moves;
}

std::vector<Move> BoardState::generatePseudoLegalMoves() const {
    std::vector<Move> moves;
    for (int sq = 0; sq < 64; sq++) {
        auto sqMoves = generatePseudoLegalMovesFrom(sq);
        moves.insert(moves.end(), sqMoves.begin(), sqMoves.end());
    }
    return moves;
}

std::vector<Move> BoardState::generateLegalMoves() const {
    std::vector<Move> pseudo = generatePseudoLegalMoves();
    std::vector<Move> legal;
    
    for (const Move& m : pseudo) {
        BoardState after = applyMove(m);
        int kingSq = after.findKing(whiteToMove);
        if (kingSq >= 0 && !after.isAttacked(kingSq, !whiteToMove)) {
            legal.push_back(m);
        }
    }
    
    return legal;
}

std::vector<Move> BoardState::generateLegalMovesFrom(int sq) const {
    std::vector<Move> pseudo = generatePseudoLegalMovesFrom(sq);
    std::vector<Move> legal;
    
    for (const Move& m : pseudo) {
        BoardState after = applyMove(m);
        int kingSq = after.findKing(whiteToMove);
        if (kingSq >= 0 && !after.isAttacked(kingSq, !whiteToMove)) {
            legal.push_back(m);
        }
    }
    
    return legal;
}

bool BoardState::isLegalMove(const Move& move) const {
    auto legal = generateLegalMovesFrom(move.from);
    for (const Move& m : legal) {
        if (m == move) return true;
    }
    return false;
}

BoardState BoardState::applyMove(const Move& move) const {
    BoardState newState = *this;
    
    Piece piece = newState.board[move.from];
    Piece captured = newState.board[move.to];
    
    newState.board[move.from] = NONE;
    newState.board[move.to] = piece;
    
    if (move.promo > 0) {
        Piece promoPieces[] = {NONE, 
            whiteToMove ? W_KNIGHT : B_KNIGHT,
            whiteToMove ? W_BISHOP : B_BISHOP,
            whiteToMove ? W_ROOK : B_ROOK,
            whiteToMove ? W_QUEEN : B_QUEEN
        };
        newState.board[move.to] = promoPieces[move.promo];
    }
    
    if ((piece == W_PAWN || piece == B_PAWN) && move.to == epSquare) {
        int capturedPawnSq = whiteToMove ? (move.to - 8) : (move.to + 8);
        newState.board[capturedPawnSq] = NONE;
    }
    
    if (piece == W_KING) {
        if (move.from == 4 && move.to == 6) {
            newState.board[7] = NONE;
            newState.board[5] = W_ROOK;
        } else if (move.from == 4 && move.to == 2) {
            newState.board[0] = NONE;
            newState.board[3] = W_ROOK;
        }
        newState.castling &= ~3;
    } else if (piece == B_KING) {
        if (move.from == 60 && move.to == 62) {
            newState.board[63] = NONE;
            newState.board[61] = B_ROOK;
        } else if (move.from == 60 && move.to == 58) {
            newState.board[56] = NONE;
            newState.board[59] = B_ROOK;
        }
        newState.castling &= ~12;
    }
    
    if (piece == W_ROOK) {
        if (move.from == 0) newState.castling &= ~2;
        else if (move.from == 7) newState.castling &= ~1;
    } else if (piece == B_ROOK) {
        if (move.from == 56) newState.castling &= ~8;
        else if (move.from == 63) newState.castling &= ~4;
    }
    
    if (move.to == 0) newState.castling &= ~2;
    else if (move.to == 7) newState.castling &= ~1;
    else if (move.to == 56) newState.castling &= ~8;
    else if (move.to == 63) newState.castling &= ~4;
    
    newState.epSquare = -1;
    if (piece == W_PAWN && rankOf(move.from) == 1 && rankOf(move.to) == 3) {
        newState.epSquare = move.from + 8;
    } else if (piece == B_PAWN && rankOf(move.from) == 6 && rankOf(move.to) == 4) {
        newState.epSquare = move.from - 8;
    }
    
    newState.whiteToMove = !whiteToMove;
    
    if (piece == W_PAWN || piece == B_PAWN || captured != NONE) {
        newState.halfmoveClock = 0;
    } else {
        newState.halfmoveClock = halfmoveClock + 1;
    }
    
    if (!whiteToMove) {
        newState.fullmoveNum = fullmoveNum + 1;
    }
    
    return newState;
}

bool BoardState::isCheckmate() const {
    return inCheck() && generateLegalMoves().empty();
}

bool BoardState::isStalemate() const {
    return !inCheck() && generateLegalMoves().empty();
}

BoardState BoardState::fromPacked(const uint8_t* data) {
    BoardState state;
    
    uint8_t flags = data[0];
    state.whiteToMove = (flags & 1) != 0;
    state.castling = (flags >> 1) & 0x0F;
    int epFile = (flags >> 5) & 0x07;
    
    if (epFile < 7) {
        int epRank = state.whiteToMove ? 5 : 2;
        state.epSquare = makeSquare(epFile, epRank);
    } else {
        state.epSquare = -1;
    }
    
    for (int i = 0; i < 32; i++) {
        uint8_t byte = data[1 + i];
        int sq1 = i * 2;
        int sq2 = i * 2 + 1;
        state.board[sq1] = static_cast<Piece>(byte & 0x0F);
        state.board[sq2] = static_cast<Piece>((byte >> 4) & 0x0F);
    }
    
    return state;
}

Puzzle Puzzle::fromRecord(const uint8_t* data, uint16_t recordSize) {
    Puzzle puzzle;
    
    puzzle.rating = data[0] | (data[1] << 8);
    
    uint8_t flags = data[2];
    uint8_t moveCount = data[3];
    
    uint8_t boardData[33];
    boardData[0] = flags;
    memcpy(boardData + 1, data + 4, 32);
    puzzle.position = BoardState::fromPacked(boardData);
    
    for (int i = 0; i < moveCount && i < 24; i++) {
        int offset = 36 + i * 2;
        uint16_t packed = data[offset] | (data[offset + 1] << 8);
        puzzle.solution.push_back(Move::unpack(packed));
    }

    puzzle.themes.clear();
    puzzle.opening.clear();
    if (recordSize >= 128) {
        puzzle.themes = readRecordField(data, 84, 32);
        puzzle.opening = readRecordField(data, 116, 12);
    }
    
    return puzzle;
}

bool PackHeader::fromFile(const uint8_t* headerData, PackHeader& out) {
    if (headerData[0] != 'C' || headerData[1] != 'P' || 
        headerData[2] != 'Z' || headerData[3] != '1') {
        return false;
    }
    
    out.recordSize = headerData[4] | (headerData[5] << 8);
    out.puzzleCount = headerData[6] | (headerData[7] << 8) | 
                      (headerData[8] << 16) | (headerData[9] << 24);
    out.ratingMin = headerData[10] | (headerData[11] << 8);
    out.ratingMax = headerData[12] | (headerData[13] << 8);
    
    return true;
}

}
