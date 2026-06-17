#include <iostream>
#include <cstdint>
#include <string>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <vector>
#include <chrono>

//  CORE DEFINITIONS 
enum NamedBitboard {
    W_PAWNS, W_KNIGHTS, W_BISHOPS, W_ROOKS, W_QUEENS, W_KING,
    B_PAWNS, B_KNIGHTS, B_BISHOPS, B_ROOKS, B_QUEENS, B_KING
};

enum Colors { WHITE, BLACK, BOTH };

const std::string squareToCoordinates[] = {
    "a1", "b1", "c1", "d1", "e1", "f1", "g1", "h1",
    "a2", "b2", "c2", "d2", "e2", "f2", "g2", "h2",
    "a3", "b3", "c3", "d3", "e3", "f3", "g3", "h3",
    "a4", "b4", "c4", "d4", "e4", "f4", "g4", "h4",
    "a5", "b5", "c5", "d5", "e5", "f5", "g5", "h5",
    "a6", "b6", "c6", "d6", "e6", "f6", "g6", "h6",
    "a7", "b7", "c7", "d7", "e7", "f7", "g7", "h7",
    "a8", "b8", "c8", "d8", "e8", "f8", "g8", "h8"
};

// Castling rights update mask array
const int castling_rights_update[64] = {
    13, 15, 15, 15, 12, 15, 15, 14, // a1-h1
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
     7, 15, 15, 15,  3, 15, 15, 11  // a8-h8
};

struct MoveList {
    int moves[256];
    int count = 0;
    void addMove(int move) { moves[count++] = move; }
};

class MoveManager {
public:
    static inline int encodeMove(int source, int target, int piece, int capture) {
        return source | (target << 6) | (piece << 12) | (capture << 16);
    }
    static inline int getSource(int move) { return move & 0x3f; }
    static inline int getTarget(int move) { return (move >> 6) & 0x3f; }
    static inline int getPiece(int move)  { return (move >> 12) & 0xf; }
    static inline bool isCapture(int move) { return (move >> 16) & 0x1; }

    static std::string moveToUCIString(int move) {
        int source = getSource(move);
        int target = getTarget(move);
        return squareToCoordinates[source] + squareToCoordinates[target];
    }
};

// THE BOARD SYSTEM 
struct BoardState {
    uint64_t bitboards[12];
    uint64_t occupancies[3];
    int sideToMove;
    int enPassantSquare;
    int castlingRights;

    BoardState() { clear(); }

    void clear() {
        for (int i = 0; i < 12; i++) bitboards[i] = 0ULL;
        for (int i = 0; i < 3; i++) occupancies[i] = 0ULL;
        sideToMove = WHITE;
        enPassantSquare = -1;
        castlingRights = 0;
    }

    void updateOccupancies() {
        occupancies[WHITE] = 0ULL; occupancies[BLACK] = 0ULL;
        for (int p = W_PAWNS; p <= W_KING; p++) occupancies[WHITE] |= bitboards[p];
        for (int p = B_PAWNS; p <= B_KING; p++) occupancies[BLACK] |= bitboards[p];
        occupancies[BOTH] = occupancies[WHITE] | occupancies[BLACK];
    }
};

class BoardManager {
public:
    static void parseFEN(BoardState &state, const std::string &fen) {
        state.clear();
        int i = 0;
        for (int rank = 7; rank >= 0; rank--) {
            for (int file = 0; file < 8; ) {
                char c = fen[i++];
                if (std::isalpha(c)) {
                    int square = rank * 8 + file;
                    if (c == 'P') state.bitboards[W_PAWNS] |= (1ULL << square);
                    else if (c == 'N') state.bitboards[W_KNIGHTS] |= (1ULL << square);
                    else if (c == 'B') state.bitboards[W_BISHOPS] |= (1ULL << square);
                    else if (c == 'R') state.bitboards[W_ROOKS] |= (1ULL << square);
                    else if (c == 'Q') state.bitboards[W_QUEENS] |= (1ULL << square);
                    else if (c == 'K') state.bitboards[W_KING] |= (1ULL << square);
                    else if (c == 'p') state.bitboards[B_PAWNS] |= (1ULL << square);
                    else if (c == 'n') state.bitboards[B_KNIGHTS] |= (1ULL << square);
                    else if (c == 'b') state.bitboards[B_BISHOPS] |= (1ULL << square);
                    else if (c == 'r') state.bitboards[B_ROOKS] |= (1ULL << square);
                    else if (c == 'q') state.bitboards[B_QUEENS] |= (1ULL << square);
                    else if (c == 'k') state.bitboards[B_KING] |= (1ULL << square);
                    file++;
                }
                else if (std::isdigit(c)) { file += (c - '0'); }
                else if (c == '/') { continue; }
            }
        }
        i++;
        state.sideToMove = (fen[i++] == 'w') ? WHITE : BLACK;
        i++;
        
        while (i < fen.length() && fen[i] != ' ') {
            char c = fen[i++];
            if (c == 'K') state.castlingRights |= 1;
            else if (c == 'Q') state.castlingRights |= 2;
            else if (c == 'k') state.castlingRights |= 4;
            else if (c == 'q') state.castlingRights |= 8;
            else if (c == '-') break;
        }
        
        //  En Passant
        if (fen[i] == ' ') i++; 
        if (i < fen.length() && fen[i] != '-') {
            int file = fen[i] - 'a';
            int rank = fen[i + 1] - '1';
            state.enPassantSquare = rank * 8 + file;
            i += 2;
        } else {
            state.enPassantSquare = -1;
            if (i < fen.length()) i++;
        }

        state.updateOccupancies();
    }
};

//  EVALUATION OF PIECES
class Evaluation {
public:
    static const int pawnValue = 100;
    static const int knightValue = 320;
    static const int bishopValue = 330;
    static const int rookValue = 500;
    static const int queenValue = 900;

    static constexpr int knightPST[64] = {
        -50, -40, -30, -30, -30, -30, -40, -50,
        -40, -20,   0,   0,   0,   0, -20, -40,
        -30,   0,  10,  15,  15,  10,   0, -30,
        -30,   5,  15,  20,  20,  15,   5, -30,
        -30,   0,  15,  20,  20,  15,   0, -30,
        -30,   5,  10,  15,  15,  10,   5, -30,
        -40, -20,   0,   5,   5,   0, -20, -40,
        -50, -40, -30, -30, -30, -30, -40, -50
    };

    static constexpr int pawnPST[64] = {
          0,   0,   0,   0,   0,   0,   0,   0,
         50,  50,  50,  50,  50,  50,  50,  50,
         10,  10,  20,  30,  30,  20,  10,  10,
          5,   5,  10,  25,  25,  10,   5,   5,
          0,   0,   0,  20,  20,   0,   0,   0,
          5,  -5, -10,   0,   0, -10,  -5,   5,
          5,  10,  10, -20, -20,  10,  10,   5,
          0,   0,   0,   0,   0,   0,   0,   0
    };

    static int evaluate(const BoardState& state) {
        int score = 0;
        score += countBits(state.bitboards[W_PAWNS]) * pawnValue;
        score += countBits(state.bitboards[W_KNIGHTS]) * knightValue;
        score += countBits(state.bitboards[W_BISHOPS]) * bishopValue;
        score += countBits(state.bitboards[W_ROOKS]) * rookValue;
        score += countBits(state.bitboards[W_QUEENS]) * queenValue;

        score += evaluatePST(state.bitboards[W_KNIGHTS], knightPST, WHITE);
        score += evaluatePST(state.bitboards[W_PAWNS], pawnPST, WHITE);

        score -= countBits(state.bitboards[B_PAWNS]) * pawnValue;
        score -= countBits(state.bitboards[B_KNIGHTS]) * knightValue;
        score -= countBits(state.bitboards[B_BISHOPS]) * bishopValue;
        score -= countBits(state.bitboards[B_ROOKS]) * rookValue;
        score -= countBits(state.bitboards[B_QUEENS]) * queenValue;

        score -= evaluatePST(state.bitboards[B_KNIGHTS], knightPST, BLACK);
        score -= evaluatePST(state.bitboards[B_PAWNS], pawnPST, BLACK);
        return score; 
    }

private:
    static inline int countBits(uint64_t bitboard) { return __builtin_popcountll(bitboard); }
    static int evaluatePST(uint64_t bitboard, const int pst[64], int color) {
        int bonus = 0;
        while (bitboard > 0) {
            int sq = __builtin_ctzll(bitboard);
            int mappedSq = (color == WHITE) ? sq : (sq ^ 56); 
            bonus += pst[mappedSq];
            bitboard &= (bitboard - 1);
        }
        return bonus;
    }
};

//  MOVE GENERATION SYSTEM 
class MoveGenerator {
private:
    uint64_t knightAttacks[64];
    uint64_t kingAttacks[64];

    uint64_t maskKnightAttacks(int square) {
        uint64_t attacks = 0ULL;
        int rank = square / 8, file = square % 8;
        int targetRank[] = {2, 2, 1, 1, -2, -2, -1, -1};
        int targetFile[] = {1, -1, 2, -2, 1, -1, 2, -2};
        for (int i = 0; i < 8; i++) {
            int r = rank + targetRank[i], f = file + targetFile[i];
            if (r >= 0 && r < 8 && f >= 0 && f < 8) attacks |= (1ULL << (r * 8 + f));
        }
        return attacks;
    }

    uint64_t maskKingAttacks(int square) {
        uint64_t attacks = 0ULL;
        int rank = square / 8, file = square % 8;
        for (int r = -1; r <= 1; r++) {
            for (int f = -1; f <= 1; f++) {
                if (r == 0 && f == 0) continue;
                int tr = rank + r, tf = file + f;
                // Changed 'f' to 'tf' below to properly map the destination coordinate
                if (tr >= 0 && tr < 8 && tf >= 0 && tf < 8) attacks |= (1ULL << (tr * 8 + tf));
            }
        }
        return attacks;
    }

public:
    MoveGenerator() {
        for (int i = 0; i < 64; i++) {
            knightAttacks[i] = maskKnightAttacks(i);
            kingAttacks[i] = maskKingAttacks(i);
        }
    }

    uint64_t getRookAttacks(int square, uint64_t occupancy) {
        uint64_t attacks = 0ULL;
        int r = square / 8, f = square % 8;
        for (int rank = r + 1; rank < 8; rank++) { attacks |= (1ULL << (rank * 8 + f)); if (occupancy & (1ULL << (rank * 8 + f))) break; }
        for (int rank = r - 1; rank >= 0; rank--) { attacks |= (1ULL << (rank * 8 + f)); if (occupancy & (1ULL << (rank * 8 + f))) break; }
        for (int file = f + 1; file < 8; file++) { attacks |= (1ULL << (r * 8 + file)); if (occupancy & (1ULL << (r * 8 + file))) break; }
        for (int file = f - 1; file >= 0; file--) { attacks |= (1ULL << (r * 8 + file)); if (occupancy & (1ULL << (r * 8 + file))) break; }
        return attacks;
    }

    uint64_t getBishopAttacks(int square, uint64_t occupancy) {
        uint64_t attacks = 0ULL;
        int r = square / 8, f = square % 8;
        for (int rank = r + 1, file = f + 1; rank < 8 && file < 8; rank++, file++) { attacks |= (1ULL << (rank * 8 + file)); if (occupancy & (1ULL << (rank * 8 + file))) break; }
        for (int rank = r + 1, file = f - 1; rank < 8 && file >= 0; rank++, file--) { attacks |= (1ULL << (rank * 8 + file)); if (occupancy & (1ULL << (rank * 8 + file))) break; }
        for (int rank = r - 1, file = f + 1; rank >= 0 && file < 8; rank--, file++) { attacks |= (1ULL << (rank * 8 + file)); if (occupancy & (1ULL << (rank * 8 + file))) break; }
        for (int rank = r - 1, file = f - 1; rank >= 0 && file >= 0; rank--, file--) { attacks |= (1ULL << (rank * 8 + file)); if (occupancy & (1ULL << (rank * 8 + file))) break; }
        return attacks;
    }

    bool isSquareAttacked(int square, int attackerSide, const BoardState &state) {
        int file = square % 8;
        if (attackerSide == WHITE) {
            if (file > 0 && square >= 9)  { if (state.bitboards[W_PAWNS] & (1ULL << (square - 9))) return true; }
            if (file < 7 && square >= 7)  { if (state.bitboards[W_PAWNS] & (1ULL << (square - 7))) return true; }
        } else {
            if (file > 0 && square <= 54) { if (state.bitboards[B_PAWNS] & (1ULL << (square + 7))) return true; }
            if (file < 7 && square <= 52) { if (state.bitboards[B_PAWNS] & (1ULL << (square + 9))) return true; }
        }
        if (knightAttacks[square] & ((attackerSide == WHITE) ? state.bitboards[W_KNIGHTS] : state.bitboards[B_KNIGHTS])) return true;
        if (kingAttacks[square] & ((attackerSide == WHITE) ? state.bitboards[W_KING] : state.bitboards[B_KING])) return true;
        
        uint64_t bishopsQueens = (attackerSide == WHITE) ? (state.bitboards[W_BISHOPS] | state.bitboards[W_QUEENS]) : (state.bitboards[B_BISHOPS] | state.bitboards[B_QUEENS]);
        if (getBishopAttacks(square, state.occupancies[BOTH]) & bishopsQueens) return true;

        uint64_t rooksQueens = (attackerSide == WHITE) ? (state.bitboards[W_ROOKS] | state.bitboards[W_QUEENS]) : (state.bitboards[B_ROOKS] | state.bitboards[B_QUEENS]);
        if (getRookAttacks(square, state.occupancies[BOTH]) & rooksQueens) return true;
        return false;
    }

    void generatePseudoLegalMoves(const BoardState &state, MoveList &moveList) {
        int side = state.sideToMove;
        uint64_t friendlyPieces = (side == WHITE) ? state.occupancies[WHITE] : state.occupancies[BLACK];
        uint64_t enemyPieces = (side == WHITE) ? state.occupancies[BLACK] : state.occupancies[WHITE];
        uint64_t totalOccupancy = state.occupancies[BOTH];

        // PAWNS
        if (side == WHITE) {
            uint64_t pawns = state.bitboards[W_PAWNS];
            while (pawns > 0) {
                int sq = __builtin_ctzll(pawns);
                int target = sq + 8;
                if (!(totalOccupancy & (1ULL << target))) {
                    moveList.addMove(MoveManager::encodeMove(sq, target, W_PAWNS, 0));
                    if (sq >= 8 && sq <= 15 && !(totalOccupancy & (1ULL << (sq + 16))))
                        moveList.addMove(MoveManager::encodeMove(sq, sq + 16, W_PAWNS, 0));
                }
                uint64_t attacks = 0ULL;
                if ((sq % 8) > 0) attacks |= (1ULL << (sq + 7));
                if ((sq % 8) < 7) attacks |= (1ULL << (sq + 9));
                
                if (state.enPassantSquare != -1) {
                    if ((sq % 8) > 0 && (sq + 7) == state.enPassantSquare) 
                        moveList.addMove(MoveManager::encodeMove(sq, state.enPassantSquare, W_PAWNS, 1));
                    if ((sq % 8) < 7 && (sq + 9) == state.enPassantSquare) 
                        moveList.addMove(MoveManager::encodeMove(sq, state.enPassantSquare, W_PAWNS, 1));
                }

                attacks &= enemyPieces;
                while (attacks > 0) {
                    int targetAtt = __builtin_ctzll(attacks);
                    if (enemyPieces & (1ULL << targetAtt)) moveList.addMove(MoveManager::encodeMove(sq, targetAtt, W_PAWNS, 1));
                    attacks &= (attacks - 1);
                }
                pawns &= (pawns - 1);
            }
        } else {
            uint64_t pawns = state.bitboards[B_PAWNS];
            while (pawns > 0) {
                int sq = __builtin_ctzll(pawns);
                int target = sq - 8;
                if (!(totalOccupancy & (1ULL << target))) {
                    moveList.addMove(MoveManager::encodeMove(sq, target, B_PAWNS, 0));
                    if (sq >= 48 && sq <= 55 && !(totalOccupancy & (1ULL << (sq - 16))))
                        moveList.addMove(MoveManager::encodeMove(sq, sq - 16, B_PAWNS, 0));
                }
                uint64_t attacks = 0ULL;
                if ((sq % 8) > 0) attacks |= (1ULL << (sq - 9));
                if ((sq % 8) < 7) attacks |= (1ULL << (sq - 7));
                
                if (state.enPassantSquare != -1) {
                    if ((sq % 8) > 0 && (sq - 9) == state.enPassantSquare) 
                        moveList.addMove(MoveManager::encodeMove(sq, state.enPassantSquare, B_PAWNS, 1));
                    if ((sq % 8) < 7 && (sq - 7) == state.enPassantSquare) 
                        moveList.addMove(MoveManager::encodeMove(sq, state.enPassantSquare, B_PAWNS, 1));
                }

                attacks &= enemyPieces;
                while (attacks > 0) {
                    int targetAtt = __builtin_ctzll(attacks);
                    if (enemyPieces & (1ULL << targetAtt)) moveList.addMove(MoveManager::encodeMove(sq, targetAtt, B_PAWNS, 1));
                    attacks &= (attacks - 1);
                }
                pawns &= (pawns - 1);
            }
        }

        // KNIGHTS
        int knightType = (side == WHITE) ? W_KNIGHTS : B_KNIGHTS;
        uint64_t knights = (side == WHITE) ? state.bitboards[W_KNIGHTS] : state.bitboards[B_KNIGHTS];
        while (knights > 0) {
            int sq = __builtin_ctzll(knights);
            uint64_t attacks = knightAttacks[sq] & ~friendlyPieces;
            while (attacks > 0) {
                int target = __builtin_ctzll(attacks);
                moveList.addMove(MoveManager::encodeMove(sq, target, knightType, (enemyPieces & (1ULL << target)) ? 1 : 0));
                attacks &= (attacks - 1);
            }
            knights &= (knights - 1);
        }

        // BISHOPS
        int bishopType = (side == WHITE) ? W_BISHOPS : B_BISHOPS;
        uint64_t bishops = (side == WHITE) ? state.bitboards[W_BISHOPS] : state.bitboards[B_BISHOPS];
        while (bishops > 0) {
            int sq = __builtin_ctzll(bishops);
            uint64_t attacks = getBishopAttacks(sq, totalOccupancy) & ~friendlyPieces;
            while (attacks > 0) {
                int target = __builtin_ctzll(attacks);
                moveList.addMove(MoveManager::encodeMove(sq, target, bishopType, (enemyPieces & (1ULL << target)) ? 1 : 0));
                attacks &= (attacks - 1);
            }
            bishops &= (bishops - 1);
        }

        // ROOKS
        int rookType = (side == WHITE) ? W_ROOKS : B_ROOKS;
        uint64_t rooks = (side == WHITE) ? state.bitboards[W_ROOKS] : state.bitboards[B_ROOKS];
        while (rooks > 0) {
            int sq = __builtin_ctzll(rooks);
            uint64_t attacks = getRookAttacks(sq, totalOccupancy) & ~friendlyPieces;
            while (attacks > 0) {
                int target = __builtin_ctzll(attacks);
                moveList.addMove(MoveManager::encodeMove(sq, target, rookType, (enemyPieces & (1ULL << target)) ? 1 : 0));
                attacks &= (attacks - 1);
            }
            rooks &= (rooks - 1);
        }

        // QUEENS
        int queenType = (side == WHITE) ? W_QUEENS : B_QUEENS;
        uint64_t queens = (side == WHITE) ? state.bitboards[W_QUEENS] : state.bitboards[B_QUEENS];
        while (queens > 0) {
            int sq = __builtin_ctzll(queens);
            uint64_t attacks = (getBishopAttacks(sq, totalOccupancy) | getRookAttacks(sq, totalOccupancy)) & ~friendlyPieces;
            while (attacks > 0) {
                int target = __builtin_ctzll(attacks);
                moveList.addMove(MoveManager::encodeMove(sq, target, queenType, (enemyPieces & (1ULL << target)) ? 1 : 0));
                attacks &= (attacks - 1);
            }
            queens &= (queens - 1);
        }

        // KING & CASTLING
        int kingType = (side == WHITE) ? W_KING : B_KING;
        uint64_t king = (side == WHITE) ? state.bitboards[W_KING] : state.bitboards[B_KING];
        if (king) {
            int sq = __builtin_ctzll(king);
            uint64_t attacks = kingAttacks[sq] & ~friendlyPieces;
            while (attacks > 0) {
                int target = __builtin_ctzll(attacks);
                moveList.addMove(MoveManager::encodeMove(sq, target, kingType, (enemyPieces & (1ULL << target)) ? 1 : 0));
                attacks &= (attacks - 1);
            }
            if (side == WHITE) {
                if ((state.castlingRights & 1) && !(totalOccupancy & ((1ULL << 5) | (1ULL << 6)))) {
                    if (!isSquareAttacked(4, BLACK, state) && !isSquareAttacked(5, BLACK, state) && !isSquareAttacked(6, BLACK, state))
                        moveList.addMove(MoveManager::encodeMove(4, 6, W_KING, 0));
                }
                if ((state.castlingRights & 2) && !(totalOccupancy & ((1ULL << 1) | (1ULL << 2) | (1ULL << 3)))) {
                    if (!isSquareAttacked(4, BLACK, state) && !isSquareAttacked(3, BLACK, state) && !isSquareAttacked(2, BLACK, state))
                        moveList.addMove(MoveManager::encodeMove(4, 2, W_KING, 0));
                }
            } else {
                if ((state.castlingRights & 4) && !(totalOccupancy & ((1ULL << 61) | (1ULL << 62)))) {
                    if (!isSquareAttacked(60, WHITE, state) && !isSquareAttacked(61, WHITE, state) && !isSquareAttacked(62, WHITE, state))
                        moveList.addMove(MoveManager::encodeMove(60, 62, B_KING, 0));
                }
                if ((state.castlingRights & 8) && !(totalOccupancy & ((1ULL << 57) | (1ULL << 58) | (1ULL << 59)))) {
                    if (!isSquareAttacked(60, WHITE, state) && !isSquareAttacked(59, WHITE, state) && !isSquareAttacked(58, WHITE, state))
                        moveList.addMove(MoveManager::encodeMove(60, 58, B_KING, 0));
                }
            }
        }
    }

    void generateLegalMoves(BoardState &state, MoveList &legalMoves) {
        MoveList pseudoMoves;
        generatePseudoLegalMoves(state, pseudoMoves);

        for (int i = 0; i < pseudoMoves.count; i++) {
            int move = pseudoMoves.moves[i];
            int src = MoveManager::getSource(move);
            int dst = MoveManager::getTarget(move);
            int piece = MoveManager::getPiece(move);

            BoardState sandbox = state;
            sandbox.bitboards[piece] &= ~(1ULL << src);
            sandbox.bitboards[piece] |= (1ULL << dst);

            if (piece == W_KING) {
                if (src == 4 && dst == 6) { 
                    sandbox.bitboards[W_ROOKS] &= ~(1ULL << 7);
                    sandbox.bitboards[W_ROOKS] |= (1ULL << 5);
                } else if (src == 4 && dst == 2) { 
                    sandbox.bitboards[W_ROOKS] &= ~(1ULL << 0);
                    sandbox.bitboards[W_ROOKS] |= (1ULL << 3);
                }
            } else if (piece == B_KING) {
                if (src == 60 && dst == 62) { 
                    sandbox.bitboards[B_ROOKS] &= ~(1ULL << 63);
                    sandbox.bitboards[B_ROOKS] |= (1ULL << 61);
                } else if (src == 60 && dst == 58) { 
                    sandbox.bitboards[B_ROOKS] &= ~(1ULL << 56);
                    sandbox.bitboards[B_ROOKS] |= (1ULL << 59);
                }
            }

            if (MoveManager::isCapture(move)) {
                if (piece == W_PAWNS && dst == state.enPassantSquare) {
                    sandbox.bitboards[B_PAWNS] &= ~(1ULL << (dst - 8));
                } else if (piece == B_PAWNS && dst == state.enPassantSquare) {
                    sandbox.bitboards[W_PAWNS] &= ~(1ULL << (dst + 8));
                } else {
                    int startEnemy = (state.sideToMove == WHITE) ? B_PAWNS : W_PAWNS;
                    int endEnemy = (state.sideToMove == WHITE) ? B_KING : W_KING;
                    for (int p = startEnemy; p <= endEnemy; p++) {
                        if (sandbox.bitboards[p] & (1ULL << dst)) {
                            sandbox.bitboards[p] &= ~(1ULL << dst);
                            break;
                        }
                    }
                }
            }

            sandbox.updateOccupancies();

            int kingBB = (state.sideToMove == WHITE) ? W_KING : B_KING;
            if (!sandbox.bitboards[kingBB]) continue;
            int kingSquare = __builtin_ctzll(sandbox.bitboards[kingBB]);

            if (!isSquareAttacked(kingSquare, (state.sideToMove == WHITE) ? BLACK : WHITE, sandbox)) {
                legalMoves.addMove(move);
            }
        }
    }
};

// SEARCH ENGINE MECHANICS 
class Search {
private:
    MoveGenerator generator;
    int nodesEvaluated = 0;

    std::chrono::time_point<std::chrono::steady_clock> startTime;
    int64_t allocatedTimeMs = 0;
    bool searchAborted = false;

    inline void checkClock() {
        if ((nodesEvaluated & 2047) == 0 && allocatedTimeMs != 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
            if (elapsed >= allocatedTimeMs) {
                searchAborted = true;
            }
        }
    }

    int scoreMove(int move) {
        if (MoveManager::isCapture(move)) return 1000; 
        return 0; 
    }

    void sortMoves(MoveList& moveList) {
        int scores[256];
        for (int i = 0; i < moveList.count; i++) scores[i] = scoreMove(moveList.moves[i]);
        for (int i = 0; i < moveList.count - 1; i++) {
            for (int j = i + 1; j < moveList.count; j++) {
                if (scores[i] < scores[j]) {
                    std::swap(scores[i], scores[j]);
                    std::swap(moveList.moves[i], moveList.moves[j]);
                }
            }
        }
    }

public:
    int quiescence(BoardState& state, int alpha, int beta, bool maximizingPlayer) {
        nodesEvaluated++;
        checkClock();
        if (searchAborted) return 0; 

        int standPat = Evaluation::evaluate(state);
        if (maximizingPlayer) {
            if (standPat >= beta) return beta;
            if (standPat > alpha) alpha = standPat;
        } else {
            if (standPat <= alpha) return alpha;
            if (standPat < beta) beta = standPat;
        }

        MoveList moveList;
        generator.generateLegalMoves(state, moveList);
        sortMoves(moveList);

        for (int i = 0; i < moveList.count; i++) {
            if (!MoveManager::isCapture(moveList.moves[i])) break; 

            BoardState nextState;
            if (!makeMove(state, nextState, moveList.moves[i])) continue;

            int score = quiescence(nextState, alpha, beta, !maximizingPlayer);
            
            if (maximizingPlayer) {
                alpha = std::max(alpha, score);
                if (alpha >= beta) return beta;
            } else {
                beta = std::min(beta, score);
                if (beta <= alpha) return alpha;
            }
        }
        return maximizingPlayer ? alpha : beta;
    }

    bool makeMove(const BoardState& current, BoardState& next, int move) {
        int src = MoveManager::getSource(move);
        int dst = MoveManager::getTarget(move);
        int piece = MoveManager::getPiece(move);

        next = current; 
        
        // Clear piece from source and map to destination
        next.bitboards[piece] &= ~(1ULL << src);
        next.bitboards[piece] |= (1ULL << dst);
        
        int prevEnPassant = current.enPassantSquare;
        next.enPassantSquare = -1;

        // Structural adjustments for Castling Moves
        if (piece == W_KING) {
            if (src == 4 && dst == 6) { 
                next.bitboards[W_ROOKS] &= ~(1ULL << 7);
                next.bitboards[W_ROOKS] |= (1ULL << 5);
            } else if (src == 4 && dst == 2) { 
                next.bitboards[W_ROOKS] &= ~(1ULL << 0);
                next.bitboards[W_ROOKS] |= (1ULL << 3);
            }
        } else if (piece == B_KING) {
            if (src == 60 && dst == 62) { 
                next.bitboards[B_ROOKS] &= ~(1ULL << 63);
                next.bitboards[B_ROOKS] |= (1ULL << 61);
            } else if (src == 60 && dst == 58) { 
                next.bitboards[B_ROOKS] &= ~(1ULL << 56);
                next.bitboards[B_ROOKS] |= (1ULL << 59);
            }
        } 
        // En Passant tracking updates
        else if (piece == W_PAWNS) {
            if (dst == src + 16) next.enPassantSquare = src + 8;
        } else if (piece == B_PAWNS) {
            if (dst == src - 16) next.enPassantSquare = src - 8;
        }

        // Clean out explicitly requested target captures
        if (MoveManager::isCapture(move)) {
            if (piece == W_PAWNS && dst == prevEnPassant) {
                next.bitboards[B_PAWNS] &= ~(1ULL << (dst - 8));
            } else if (piece == B_PAWNS && dst == prevEnPassant) {
                next.bitboards[W_PAWNS] &= ~(1ULL << (dst + 8));
            } else {
                int startEnemy = (current.sideToMove == WHITE) ? B_PAWNS : W_PAWNS;
                int endEnemy = (current.sideToMove == WHITE) ? B_KING : W_KING;
                for (int p = startEnemy; p <= endEnemy; p++) {
                    if (next.bitboards[p] & (1ULL << dst)) {
                        next.bitboards[p] &= ~(1ULL << dst);
                        break;
                    }
                }
            }
        }

        // Sweeper checks to clear destination overlaps on enemy arrays
        int enemyStart = (current.sideToMove == WHITE) ? B_PAWNS : W_PAWNS;
        int enemyEnd = (current.sideToMove == WHITE) ? B_KING : W_KING;
        for (int p = enemyStart; p <= enemyEnd; p++) {
            if (p != piece && (next.bitboards[p] & (1ULL << dst))) {
                next.bitboards[p] &= ~(1ULL << dst);
            }
        }

        next.castlingRights &= castling_rights_update[src];
        next.castlingRights &= castling_rights_update[dst];

        next.updateOccupancies();

        int friendlyKing = (current.sideToMove == WHITE) ? W_KING : B_KING;
        if (!next.bitboards[friendlyKing]) return false;
        int kingSq = __builtin_ctzll(next.bitboards[friendlyKing]);
        
        int enemyColor = (current.sideToMove == WHITE) ? BLACK : WHITE;
        if (generator.isSquareAttacked(kingSq, enemyColor, next)) return false; 

        next.sideToMove = enemyColor; 
        return true;
    }

    int alphaBeta(BoardState& state, int depth, int alpha, int beta, bool maximizingPlayer) {
        nodesEvaluated++;
        checkClock();
        if (searchAborted) return 0; 

        if (depth == 0) return quiescence(state, alpha, beta, maximizingPlayer);

        MoveList moveList;
        generator.generateLegalMoves(state, moveList);
        sortMoves(moveList); 

        if (moveList.count == 0) {
            int kingBB = (state.sideToMove == WHITE) ? W_KING : B_KING;
            int kingSq = __builtin_ctzll(state.bitboards[kingBB]);
            if (generator.isSquareAttacked(kingSq, (state.sideToMove == WHITE) ? BLACK : WHITE, state)) {
                return maximizingPlayer ? -100000 + (8 - depth) : 100000 - (8 - depth); 
            }
            return 0; 
        }

        if (maximizingPlayer) {
            int maxEval = -999999;
            for (int i = 0; i < moveList.count; i++) {
                BoardState nextState;
                if (!makeMove(state, nextState, moveList.moves[i])) continue;

                int eval = alphaBeta(nextState, depth - 1, alpha, beta, false);
                if (searchAborted) return 0; 

                maxEval = std::max(maxEval, eval);
                alpha = std::max(alpha, eval);
                if (beta <= alpha) break; 
            }
            return maxEval;
        } else {
            int minEval = 999999;
            for (int i = 0; i < moveList.count; i++) {
                BoardState nextState;
                if (!makeMove(state, nextState, moveList.moves[i])) continue;

                int eval = alphaBeta(nextState, depth - 1, alpha, beta, true);
                if (searchAborted) return 0; 

                minEval = std::min(minEval, eval);
                beta = std::min(beta, eval);
                if (beta <= alpha) break; 
            }
            return minEval;
        }
    }

    int getBestMove(BoardState& state, int targetDepth, int64_t maxTimeMs) {
        startTime = std::chrono::steady_clock::now();
        allocatedTimeMs = maxTimeMs;
        searchAborted = false;
        nodesEvaluated = 0;

        int finalBestMove = 0;
        MoveList moveList;
        generator.generateLegalMoves(state, moveList);
        sortMoves(moveList);

        if (moveList.count == 0) return 0; 
        finalBestMove = moveList.moves[0]; 

        bool isWhite = (state.sideToMove == WHITE);

        for (int currentDepth = 1; currentDepth <= targetDepth; currentDepth++) {
            int currentDepthBestMove = 0;
            int bestScore = isWhite ? -999999 : 999999;

            for (int i = 0; i < moveList.count; i++) {
                BoardState nextState;
                if (!makeMove(state, nextState, moveList.moves[i])) continue;

                int score = alphaBeta(nextState, currentDepth - 1, -999999, 999999, !isWhite);
                if (searchAborted) break; 

                if (isWhite) {
                    if (score > bestScore) {
                        bestScore = score;
                        currentDepthBestMove = moveList.moves[i];
                    }
                } else {
                    if (score < bestScore) {
                        bestScore = score;
                        currentDepthBestMove = moveList.moves[i];
                    }
                }
            }

            if (!searchAborted) {
                finalBestMove = currentDepthBestMove;
                auto rightNow = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(rightNow - startTime).count();
                
                std::cout << "info depth " << currentDepth 
                          << " score cp " << bestScore 
                          << " nodes " << nodesEvaluated 
                          << " time " << duration << "\n";

                for (int i = 0; i < moveList.count; i++) {
                    if (moveList.moves[i] == finalBestMove) {
                        std::swap(moveList.moves[0], moveList.moves[i]);
                        break;
                    }
                }
            } else {
                break; 
            }
        }
        return finalBestMove;
    }
};

// UCI ENGINE LOOP 
void uciLoop() {
    std::setvbuf(stdin, NULL, _IONBF, 0);
    std::setvbuf(stdout, NULL, _IONBF, 0);

    BoardState state;
    MoveGenerator generator;
    Search engineSearch;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "uci") {
            std::cout << "id name ShahidEngine v2.0\n";
            std::cout << "id author Mohamed Shahid\n";
            std::cout << "uciok\n";
        } 
        else if (line == "isready") {
            std::cout << "readyok\n";
        } 
        else if (line.rfind("position", 0) == 0) {
            std::string token;
            std::istringstream diff(line);
            diff >> token; 

            diff >> token;
            if (token == "startpos") {
                std::string startFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
                BoardManager::parseFEN(state, startFen);
                diff >> token;
            } 
            else if (token == "fen") {
                std::string fenString = "";
                for (int i = 0; i < 6; i++) {
                    diff >> token;
                    fenString += token + " ";
                }
                BoardManager::parseFEN(state, fenString);
                diff >> token;
            }

            if (token == "moves") {
                while (diff >> token) {
                    MoveList legalList;
                    generator.generateLegalMoves(state, legalList);
                    for (int m = 0; m < legalList.count; m++) {
                        int move = legalList.moves[m];
                        if (MoveManager::moveToUCIString(move) == token) {
                            BoardState nextState;
                            engineSearch.makeMove(state, nextState, move);
                            state = nextState;
                            break;
                        }
                    }
                }
            }
        }
        else if (line.rfind("go", 0) == 0) {
            int bestMove = engineSearch.getBestMove(state, 5, 5000); 
            std::cout << "bestmove " << MoveManager::moveToUCIString(bestMove) << "\n";
        }
        else if (line == "quit") {
            break;
        }
    }
}

int main() {
    uciLoop();
    return 0;
}