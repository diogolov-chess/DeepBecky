/*
 * This file is part of Deep Becky 1.2 - A UCI Chess Engine written by AI
 * Copyright (C) 2025-2026 Diogo de Oliveira Almeida.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

// position.cpp - Position class implementation
#include "position.h"
#include "tt.h"
#include "magic.h"
#include <algorithm>
#include <sstream>
#include <iostream>
#include <cctype>
#include <random>

// ========================= Zobrist =========================
Zobrist::Zobrist() {
    std::mt19937_64 rng(0xD10D10D10ULL ^ 0xC0FFEEBADBEEFULL);
    for (int p = 0; p < PIECE_NB; p++)
        for (int s = 0; s < 64; s++)
            piece[p][s] = rng();
    side = rng();
    for (int i = 0; i < 16; i++) castling[i] = rng();
    for (int i = 0; i < 9; i++) ep[i] = rng();
}
Zobrist ZOB;

// ========================= Hash Tables =========================
PawnEntry pawnTable[PAWN_TT_SIZE];
MaterialEntry materialTable[MATERIAL_TT_SIZE];
KillerTable killers;
int history_heur[2][64][64];

// ========================= SEE Helpers =========================
namespace {

// Fast attackers calculation - returns all attackers to a square
// This is used by the optimized SEE function
U64 allAttackersTo(int sq, U64 occ, const U64 pieces[PIECE_NB]) {
    U64 attackers = 0;
    // White attackers
    attackers |= pieces[WPAWN] & BPAWN_ATK_BB[sq];
    attackers |= pieces[WKNIGHT] & KNIGHT_ATK_BB[sq];
    attackers |= pieces[WKING] & KING_ATK_BB[sq];
    // Black attackers
    attackers |= pieces[BPAWN] & WPAWN_ATK_BB[sq];
    attackers |= pieces[BKNIGHT] & KNIGHT_ATK_BB[sq];
    attackers |= pieces[BKING] & KING_ATK_BB[sq];
    // Sliders (both colors at once)
    U64 bishopsQueens = pieces[WBISHOP] | pieces[WQUEEN] | pieces[BBISHOP] | pieces[BQUEEN];
    attackers |= bishopsQueens & Magic::bishopAttacks(sq, occ);
    U64 rooksQueens = pieces[WROOK] | pieces[WQUEEN] | pieces[BROOK] | pieces[BQUEEN];
    attackers |= rooksQueens & Magic::rookAttacks(sq, occ);
    return attackers;
}

} // namespace

// ========================= Constructor =========================
Position::Position() {
    initBitboards();
    Magic::init();
    clearTT();
    clearHeuristics();
    repetitionHistory.reserve(MAX_STACK);
    plies_since_null = 0;
    setStartPos();
}

// ========================= Hash =========================
uint64_t Position::computeHash() const {
    uint64_t h = 0;
    for (int p = WPAWN; p <= BKING; ++p) {
        U64 bb = bitboards[p];
        while (bb) {
            int s = pop_lsb(&bb);
            h ^= ZOB.piece[p][s];
        }
    }
    if (!white_to_move) h ^= ZOB.side;
    h ^= ZOB.castling[castling & 15];
    h ^= ZOB.ep[ep_file & 15];
    return h;
}

// ========================= FEN =========================
void Position::setStartPos() {
    if (!setFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")) {
        std::cout << "info string falha ao carregar FEN inicial" << std::endl;
    }
}

bool Position::setFEN(const std::string& fen) {
    std::vector<std::string> tokens;
    tokens.reserve(6);
    std::string token;
    std::stringstream ss(fen);
    while (ss >> token) {
        if (tokens.size() < 6) tokens.push_back(token);
    }

    if (tokens.size() < 4) return false;
    while (tokens.size() < 6) {
        tokens.push_back(tokens.size() == 4 ? "0" : "1");
    }

    std::memset(bitboards, 0, sizeof(bitboards));
    std::memset(color_bitboards, 0, sizeof(color_bitboards));
    std::memset(piece_board, EMPTY, sizeof(piece_board));

    const std::string& placements = tokens[0];
    const std::string& side = tokens[1];
    const std::string& castl_str = tokens[2];
    const std::string& ep_str = tokens[3];

    auto charToPiece = [](char c) -> int {
        switch (c) {
            case 'P': return WPAWN;   case 'N': return WKNIGHT; case 'B': return WBISHOP;
            case 'R': return WROOK;   case 'Q': return WQUEEN;  case 'K': return WKING;
            case 'p': return BPAWN;   case 'n': return BKNIGHT; case 'b': return BBISHOP;
            case 'r': return BROOK;   case 'q': return BQUEEN;  case 'k': return BKING;
            default:  return EMPTY;
        }
    };

    int file = 0;
    int rank = 7;
    king_sq[WHITE] = -1;
    king_sq[BLACK] = -1;

    for (char c : placements) {
        if (c == '/') {
            if (file != 8) return false;
            if (--rank < 0) return false;
            file = 0;
            continue;
        }
        if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return false;
            continue;
        }
        int piece = charToPiece(c);
        if (piece == EMPTY) return false;
        if (file >= 8 || rank < 0) return false;
        int sqi = sq(file, rank);
        set_bit(bitboards[piece], sqi);
        piece_board[sqi] = piece;
        if (isWhitePiece(piece)) set_bit(color_bitboards[WHITE], sqi);
        else set_bit(color_bitboards[BLACK], sqi);
        if (piece == WKING) king_sq[WHITE] = sqi;
        if (piece == BKING) king_sq[BLACK] = sqi;
        ++file;
    }

    if (rank != 0 || file != 8) return false;
    if (king_sq[WHITE] < 0 || king_sq[BLACK] < 0) return false;

    if (side.empty()) return false;
    char sideChar = static_cast<char>(std::tolower(static_cast<unsigned char>(side[0])));
    if (sideChar == 'w') white_to_move = true;
    else if (sideChar == 'b') white_to_move = false;
    else return false;

    castling = 0;
    if (castl_str != "-") {
        for (char c : castl_str) {
            switch (c) {
                case 'K': castling |= 8; break;
                case 'Q': castling |= 4; break;
                case 'k': castling |= 2; break;
                case 'q': castling |= 1; break;
                default: return false;
            }
        }
    }

    ep_file = 0;
    if (ep_str != "-") {
        if (ep_str.size() != 2) return false;
        char fileChar = ep_str[0];
        char rankChar = ep_str[1];
        if (fileChar < 'a' || fileChar > 'h') return false;
        if (rankChar < '1' || rankChar > '8') return false;
        int fileIdx = fileChar - 'a';
        int rankIdx = rankChar - '1';
        if (rankChar == '3' || rankChar == '6') {
            int expectedRank = white_to_move ? 5 : 2;
            if (rankIdx == expectedRank) ep_file = fileIdx + 1;
        }
    }

    auto parseUnsigned = [](const std::string& s, int fallback) -> int {
        if (s.empty()) return fallback;
        int value = 0;
        for (char ch : s) {
            if (ch < '0' || ch > '9') return fallback;
            value = value * 10 + (ch - '0');
            if (value < 0) return fallback;
        }
        return value;
    };

    halfmove = parseUnsigned(tokens[4], 0);
    if (halfmove < 0) halfmove = 0;

    fullmove = parseUnsigned(tokens[5], 1);
    if (fullmove < 1) fullmove = 1;

    uci_history.clear();
    undoTop = 0;
    hash = computeHash();

    // Compute pawn key
    pawnKey = 0;
    for (int sqi = 0; sqi < 64; ++sqi) {
        int p = piece_board[sqi];
        if (p == WPAWN || p == BPAWN) {
            pawnKey ^= ZOB.piece[p][sqi];
        }
    }

    // Compute material key
    materialKey = 0;
    for (int p = WPAWN; p <= BKING; ++p) {
        U64 bb = bitboards[p];
        int count = popcount(bb);
        for (int c = 0; c < count; ++c) {
            materialKey ^= ZOB.piece[p][c];
        }
    }

    repetitionHistory.clear();
    repetitionHistory.push_back({hash, 0});
    plies_since_null = 0;
    return true;
}

// ========================= Make/Undo Move =========================
void Position::makeMove(const Move& m) {
    Undo& u = undoStack[undoTop++];
    u.castling_before = castling;
    u.ep_before = ep_file;
    u.half_before = halfmove;
    u.fullmove_before = fullmove;
    u.hash_before = hash;
    u.pawnKey_before = pawnKey;
    u.captured_piece = EMPTY;
    u.moved_piece = EMPTY;
    u.repIndexBefore = repetitionHistory.size();
    u.repetition_before = repetitionHistory.empty() ? 0 : repetitionHistory.back().repetition;
    u.plies_from_null_before = plies_since_null;
    u.was_null = false;

    int from_sq = moveFrom(m);
    int to_sq = moveTo(m);
    int piece = piece_board[from_sq];
    int us = white_to_move ? WHITE : BLACK;
    int them = us ^ 1;

    u.moved_piece = piece;
    u.captured_piece = moveIsEnPassant(m) ? (white_to_move ? BPAWN : WPAWN) : piece_board[to_sq];

    hash ^= ZOB.castling[castling & 15];
    if (ep_file > 0) hash ^= ZOB.ep[ep_file & 15];
    hash ^= ZOB.side;

    pop_bit(bitboards[piece], from_sq);
    pop_bit(color_bitboards[us], from_sq);
    hash ^= ZOB.piece[piece][from_sq];
    piece_board[from_sq] = EMPTY;

    if (piece == WPAWN || piece == BPAWN) {
        pawnKey ^= ZOB.piece[piece][from_sq];
    }

    if (u.captured_piece != EMPTY) {
        int cap_sq = to_sq;
        if (moveIsEnPassant(m)) {
            cap_sq = to_sq + (us == WHITE ? -8 : 8);
        }
        pop_bit(bitboards[u.captured_piece], cap_sq);
        pop_bit(color_bitboards[them], cap_sq);
        hash ^= ZOB.piece[u.captured_piece][cap_sq];
        piece_board[cap_sq] = EMPTY;

        if (u.captured_piece == WPAWN || u.captured_piece == BPAWN) {
            pawnKey ^= ZOB.piece[u.captured_piece][cap_sq];
        }
    }

    int landing_piece = movePromotion(m);
    if (landing_piece == 0) landing_piece = piece;

    set_bit(bitboards[landing_piece], to_sq);
    set_bit(color_bitboards[us], to_sq);
    hash ^= ZOB.piece[landing_piece][to_sq];
    piece_board[to_sq] = landing_piece;

    if (landing_piece == WPAWN || landing_piece == BPAWN) {
        pawnKey ^= ZOB.piece[landing_piece][to_sq];
    }

    if (moveIsCastle(m)) {
        int r_from, r_to;
        int rook_piece = us == WHITE ? WROOK : BROOK;
        if (to_sq > from_sq) {
            r_from = to_sq + 1;
            r_to = to_sq - 1;
        } else {
            r_from = to_sq - 2;
            r_to = to_sq + 1;
        }
        pop_bit(bitboards[rook_piece], r_from);
        pop_bit(color_bitboards[us], r_from);
        hash ^= ZOB.piece[rook_piece][r_from];
        piece_board[r_from] = EMPTY;

        set_bit(bitboards[rook_piece], r_to);
        set_bit(color_bitboards[us], r_to);
        hash ^= ZOB.piece[rook_piece][r_to];
        piece_board[r_to] = rook_piece;
    }

    ep_file = 0;
    if (moveIsDoublePush(m)) {
        ep_file = sq_x(from_sq) + 1;
    }

    castling &= ~((piece == WKING) * 12 | (piece == BKING) * 3);
    castling &= ~((from_sq == sq(7, 0) || to_sq == sq(7, 0)) * 8);
    castling &= ~((from_sq == sq(0, 0) || to_sq == sq(0, 0)) * 4);
    castling &= ~((from_sq == sq(7, 7) || to_sq == sq(7, 7)) * 2);
    castling &= ~((from_sq == sq(0, 7) || to_sq == sq(0, 7)) * 1);

    if (piece == WKING || piece == BKING) king_sq[us] = to_sq;

    if (piece == WPAWN || piece == BPAWN || moveIsCapture(m)) halfmove = 0;
    else halfmove++;
    if (!white_to_move) fullmove++;

    white_to_move = !white_to_move;
    hash ^= ZOB.castling[castling & 15];
    if (ep_file > 0) hash ^= ZOB.ep[ep_file & 15];

    bool irreversible = (piece == WPAWN || piece == BPAWN || u.captured_piece != EMPTY);
    plies_since_null = irreversible ? 0 : (u.plies_from_null_before + 1);

    int repetitionValue = 0;
    int end = std::min(halfmove, plies_since_null);
    const size_t sizeBefore = u.repIndexBefore;
    if (end >= 4 && sizeBefore >= 4) {
        for (int dist = 4; dist <= end; dist += 2) {
            if (dist > static_cast<int>(sizeBefore)) break;
            size_t idx = sizeBefore - dist;
            const RepState& prevState = repetitionHistory[idx];
            if (prevState.key == hash) {
                repetitionValue = prevState.repetition != 0 ? -dist : dist;
                break;
            }
        }
    }
    repetitionHistory.push_back({hash, repetitionValue});
}

void Position::undoMove(const Move& m) {
    Undo& u = undoStack[--undoTop];
    white_to_move = !white_to_move;
    castling = u.castling_before;
    ep_file = u.ep_before;
    halfmove = u.half_before;
    fullmove = u.fullmove_before;
    hash = u.hash_before;
    pawnKey = u.pawnKey_before;

    int from_sq = moveFrom(m);
    int to_sq = moveTo(m);
    int us = white_to_move ? WHITE : BLACK;
    int them = us ^ 1;

    if (moveIsCastle(m)) {
        int r_from, r_to;
        int rook_piece = us == WHITE ? WROOK : BROOK;
        if (to_sq > from_sq) {
            r_from = to_sq + 1;
            r_to = to_sq - 1;
        } else {
            r_from = to_sq - 2;
            r_to = to_sq + 1;
        }
        set_bit(bitboards[rook_piece], r_from);
        set_bit(color_bitboards[us], r_from);
        piece_board[r_from] = rook_piece;

        pop_bit(bitboards[rook_piece], r_to);
        pop_bit(color_bitboards[us], r_to);
        piece_board[r_to] = EMPTY;
    }

    int landing_piece = movePromotion(m) ? movePromotion(m) : u.moved_piece;
    pop_bit(bitboards[landing_piece], to_sq);
    pop_bit(color_bitboards[us], to_sq);
    piece_board[to_sq] = EMPTY;

    set_bit(bitboards[u.moved_piece], from_sq);
    set_bit(color_bitboards[us], from_sq);
    piece_board[from_sq] = u.moved_piece;

    if (u.captured_piece != EMPTY) {
        int cap_sq = moveIsEnPassant(m) ? (to_sq + (us == WHITE ? -8 : 8)) : to_sq;
        set_bit(bitboards[u.captured_piece], cap_sq);
        set_bit(color_bitboards[them], cap_sq);
        piece_board[cap_sq] = u.captured_piece;
        if (!moveIsEnPassant(m)) {
            piece_board[to_sq] = u.captured_piece;
        }
    }

    if (u.moved_piece == WKING || u.moved_piece == BKING) {
        king_sq[us] = from_sq;
    }

    repetitionHistory.resize(u.repIndexBefore);
    plies_since_null = u.plies_from_null_before;
}

// ========================= Null Move =========================
void Position::makeNullMove() {
    Undo& u = undoStack[undoTop++];
    u.castling_before = castling;
    u.ep_before = ep_file;
    u.half_before = halfmove;
    u.fullmove_before = fullmove;
    u.hash_before = hash;
    u.captured_piece = EMPTY;
    u.moved_piece = EMPTY;
    u.repIndexBefore = repetitionHistory.size();
    u.repetition_before = repetitionHistory.empty() ? 0 : repetitionHistory.back().repetition;
    u.plies_from_null_before = plies_since_null;
    u.was_null = true;

    if (ep_file > 0) hash ^= ZOB.ep[ep_file & 15];
    ep_file = 0;
    white_to_move = !white_to_move;
    hash ^= ZOB.side;
    halfmove++;
    plies_since_null = 0;

    int repetitionValue = 0;
    int limit = std::min(halfmove, plies_since_null);
    const size_t sizeBefore = u.repIndexBefore;
    if (limit >= 2 && sizeBefore > 0) {
        for (int dist = 2; dist <= limit; dist += 2) {
            if (dist > static_cast<int>(sizeBefore)) break;
            size_t idx = sizeBefore - dist;
            const RepState& prevState = repetitionHistory[idx];
            if (prevState.key == hash) {
                repetitionValue = prevState.repetition != 0 ? -dist : dist;
                break;
            }
        }
    }
    repetitionHistory.push_back({hash, repetitionValue});
}

void Position::undoNullMove() {
    Undo& u = undoStack[--undoTop];
    castling = u.castling_before;
    ep_file = u.ep_before;
    halfmove = u.half_before;
    fullmove = u.fullmove_before;
    white_to_move = !white_to_move;
    hash = u.hash_before;
    repetitionHistory.resize(u.repIndexBefore);
    plies_since_null = u.plies_from_null_before;
}

// ========================= UCI Helpers =========================
std::string Position::moveToUCI(const Move& m) const {
    auto alg = [&](int sq) {
        std::string s;
        s.push_back(static_cast<char>('a' + sq_x(sq)));
        s.push_back(static_cast<char>('1' + sq_y(sq)));
        return s;
    };
    std::string u = alg(moveFrom(m)) + alg(moveTo(m));
    int promo = movePromotion(m);
    if (promo) {
        switch (promo) {
            case WQUEEN: case BQUEEN: u += 'q'; break;
            case WROOK:  case BROOK:  u += 'r'; break;
            case WBISHOP: case BBISHOP: u += 'b'; break;
            case WKNIGHT: case BKNIGHT: u += 'n'; break;
        }
    }
    return u;
}

Move Position::uciToMove(const std::string& s) {
    Move m = MOVE_NONE;
    if (s.size() < 4) return m;

    std::string norm = s;
    for (char& ch : norm) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    auto valid_file = [](char c) { return c >= 'a' && c <= 'h'; };
    auto valid_rank = [](char c) { return c >= '1' && c <= '8'; };
    if (!valid_file(norm[0]) || !valid_rank(norm[1]) || !valid_file(norm[2]) || !valid_rank(norm[3])) {
        return m;
    }

    int from_sq = sq(norm[0] - 'a', norm[1] - '1');
    int to_sq = sq(norm[2] - 'a', norm[3] - '1');

    auto match_promo = [&](const Move& cand) -> bool {
        int promo = movePromotion(cand);
        if (norm.size() < 5) return promo == 0;
        char promo_char = norm[4];
        int promo_piece_type = 0;
        if (promo_char == 'q') promo_piece_type = white_to_move ? WQUEEN : BQUEEN;
        else if (promo_char == 'r') promo_piece_type = white_to_move ? WROOK : BROOK;
        else if (promo_char == 'b') promo_piece_type = white_to_move ? WBISHOP : BBISHOP;
        else if (promo_char == 'n') promo_piece_type = white_to_move ? WKNIGHT : BKNIGHT;
        else return false;
        return promo == promo_piece_type;
    };

    Move pseudo[MAX_MOVES];
    int pseudoCount = generatePseudo(pseudo, false);
    for (int i = 0; i < pseudoCount; ++i) {
        const Move& cand = pseudo[i];
        if (moveFrom(cand) == from_sq && moveTo(cand) == to_sq && match_promo(cand)) {
            return cand;
        }
    }

    Move legal[MAX_MOVES];
    int legalCount = generateLegal(legal);
    for (int i = 0; i < legalCount; ++i) {
        const Move& cand = legal[i];
        if (moveFrom(cand) == from_sq && moveTo(cand) == to_sq && match_promo(cand)) {
            return cand;
        }
    }

    return m;
}

// ========================= SEE =========================
// Static Exchange Evaluation - returns the estimated material gain/loss
// Uses alpha-beta style pruning with early exits
int Position::see(const Move& m) const {
    if (!moveIsCapture(m)) return 0;

    const int from_sq = moveFrom(m);
    const int to_sq = moveTo(m);
    const int side = white_to_move ? WHITE : BLACK;

    int movingPiece = piece_board[from_sq];
    if (movingPiece == EMPTY) return 0;

    int capturedPiece;
    if (moveIsEnPassant(m)) {
        capturedPiece = (side == WHITE) ? BPAWN : WPAWN;
    } else {
        capturedPiece = piece_board[to_sq];
    }
    if (capturedPiece == EMPTY) return 0;

    int promotion = movePromotion(m);
    if (promotion) movingPiece = promotion;

    // Initial swap value
    int swap = PIECE_VALUE[capturedPiece];
    
    // If we promote, add promotion value minus pawn value
    if (promotion) {
        swap += PIECE_VALUE[promotion] - PIECE_VALUE[(side == WHITE) ? WPAWN : BPAWN];
    }
    
    // If capturing with a piece worth more than what we're capturing,
    // we need to check if opponent can recapture
    
    // Setup occupancy for X-ray attacks
    U64 occ = pieces();
    occ ^= (1ULL << from_sq);  // Remove our piece from origin
    
    if (moveIsEnPassant(m)) {
        int ep_sq = to_sq + (side == WHITE ? -8 : 8);
        occ ^= (1ULL << ep_sq);  // Remove en passant captured pawn
    }
    
    // Get all attackers to the target square (using global bitboards, not copied)
    U64 allAttackers = allAttackersTo(to_sq, occ, bitboards);
    allAttackers &= occ;  // Only consider pieces still on board
    
    // Remove our piece from attackers (it's on to_sq now conceptually)
    allAttackers &= ~(1ULL << from_sq);
    
    // Color bitboards for filtering
    U64 whitePieces = color_bitboards[WHITE] & occ;
    U64 blackPieces = color_bitboards[BLACK] & occ;
    
    int stm = side ^ 1;  // Opponent to move
    int currentPieceValue = PIECE_VALUE[movingPiece];
    
    int gain[32];
    int depth = 0;
    gain[0] = swap;
    
    while (allAttackers) {
        depth++;
        gain[depth] = currentPieceValue - gain[depth - 1];
        
        // Stand pat pruning - if we're already winning enough, stop
        if (std::max(-gain[depth - 1], gain[depth]) < 0) break;
        
        // Get this side's attackers
        U64 stmPieces = (stm == WHITE) ? whitePieces : blackPieces;
        U64 stmAttackers = allAttackers & stmPieces;
        
        if (!stmAttackers) break;
        
        // Find least valuable attacker
        int attackerSq = -1;
        int attackerPiece = EMPTY;
        
        // Check piece types in order of value
        static const int pieceOrder[2][6] = {
            {WPAWN, WKNIGHT, WBISHOP, WROOK, WQUEEN, WKING},
            {BPAWN, BKNIGHT, BBISHOP, BROOK, BQUEEN, BKING}
        };
        
        for (int i = 0; i < 6; i++) {
            int pt = pieceOrder[stm][i];
            U64 bb = bitboards[pt] & stmAttackers;
            if (bb) {
                attackerSq = lsb_index(bb);
                attackerPiece = pt;
                break;
            }
        }
        
        if (attackerSq == -1) break;
        
        currentPieceValue = PIECE_VALUE[attackerPiece];
        
        // Remove this attacker from occupancy
        occ ^= (1ULL << attackerSq);
        
        // Update attacker bitboards for X-ray pieces
        if (attackerPiece == WPAWN || attackerPiece == BPAWN ||
            attackerPiece == WBISHOP || attackerPiece == BBISHOP ||
            attackerPiece == WQUEEN || attackerPiece == BQUEEN) {
            // Diagonal X-ray
            U64 diag = bitboards[WBISHOP] | bitboards[WQUEEN] | bitboards[BBISHOP] | bitboards[BQUEEN];
            allAttackers |= Magic::bishopAttacks(to_sq, occ) & diag;
        }
        if (attackerPiece == WROOK || attackerPiece == BROOK ||
            attackerPiece == WQUEEN || attackerPiece == BQUEEN) {
            // Straight X-ray
            U64 straight = bitboards[WROOK] | bitboards[WQUEEN] | bitboards[BROOK] | bitboards[BQUEEN];
            allAttackers |= Magic::rookAttacks(to_sq, occ) & straight;
        }
        
        allAttackers &= occ;
        
        // Update color occupancy
        if (stm == WHITE) whitePieces ^= (1ULL << attackerSq);
        else blackPieces ^= (1ULL << attackerSq);
        
        stm ^= 1;
        
        if (depth >= 31) break;
    }
    
    // Minimax back the gains
    while (depth > 0) {
        gain[depth - 1] = -std::max(-gain[depth - 1], gain[depth]);
        depth--;
    }
    
    return gain[0];
}

// ========================= Misc =========================
void Position::clearTT() {
    TT.clear();
}

void Position::initBook() {}
std::string Position::bookKey() const { return ""; }

bool Position::timeUp() const {
    if (time_limit_ms <= 0) return false;
    auto now = std::chrono::high_resolution_clock::now();
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
    return ms >= time_limit_ms;
}

bool Position::isFiftyMoveDraw() const {
    return halfmove >= 100;
}

bool Position::isThreefoldRepetition() const {
    if (repetitionHistory.empty()) return false;
    return repetitionHistory.back().repetition < 0;
}

bool Position::isThreefoldRepetition(int ply) const {
    if (repetitionHistory.empty()) return false;
    int rep = repetitionHistory.back().repetition;
    if (rep == 0) return false;
    if (ply <= 0) return rep < 0;
    return rep < ply;
}

bool Position::hasGameCycle(int ply) const {
    if (ply <= 0) return false;
    const size_t historySize = repetitionHistory.size();
    if (historySize < 5) return false;

    size_t current = historySize - 1;
    int end = std::min(halfmove, plies_since_null);
    if (end < 3) return false;

    for (int dist = 4; dist <= end; dist += 2) {
        if (dist > static_cast<int>(current)) break;
        const RepState& prev = repetitionHistory[current - dist];
        if (prev.key != hash) continue;
        if (ply > dist) return true;
        if (prev.repetition != 0) return true;
    }
    return false;
}

bool Position::isInsufficientMaterial() const {
    if ((bitboards[WPAWN] | bitboards[BPAWN] | bitboards[WROOK] | bitboards[BROOK] |
         bitboards[WQUEEN] | bitboards[BQUEEN]) != 0)
        return false;

    const int whiteBishops = popcount(bitboards[WBISHOP]);
    const int blackBishops = popcount(bitboards[BBISHOP]);
    const int whiteKnights = popcount(bitboards[WKNIGHT]);
    const int blackKnights = popcount(bitboards[BKNIGHT]);

    const int whiteMinor = whiteBishops + whiteKnights;
    const int blackMinor = blackBishops + blackKnights;

    if (whiteMinor == 0 && blackMinor == 0) return true;
    if (whiteMinor == 1 && blackMinor == 0) return true;
    if (whiteMinor == 0 && blackMinor == 1) return true;

    if (whiteMinor == 1 && blackMinor == 1) {
        if (whiteBishops == 1 && blackBishops == 1 && whiteKnights == 0 && blackKnights == 0) {
            U64 bishops = bitboards[WBISHOP] | bitboards[BBISHOP];
            bool hasLight = false;
            bool hasDark = false;
            while (bishops) {
                int s = pop_lsb(&bishops);
                if (isLightSquare(s)) hasLight = true;
                else hasDark = true;
            }
            return !(hasLight && hasDark);
        }
    }

    return false;
}

bool Position::isDraw(int ply) {
    if (halfmove >= 100) return true;
    if (isThreefoldRepetition(ply)) return true;
    // Skip expensive isInsufficientMaterial in most cases
    // Only check if very few pieces remain (fast check first)
    int pieceCount = popcount(color_bitboards[WHITE] | color_bitboards[BLACK]);
    if (pieceCount <= 4 && isInsufficientMaterial()) return true;
    return false;
}

// ========================= SEE Wrapper =========================
bool Position::SEE(const Move& m, int threshold) const {
    if (!moveIsCapture(m)) return 0 >= threshold;

    const int from_sq = moveFrom(m);
    const int to_sq = moveTo(m);
    const int side = white_to_move ? WHITE : BLACK;

    int movingPiece = piece_board[from_sq];
    if (movingPiece == EMPTY) return false;

    int capturedPiece;
    if (moveIsEnPassant(m)) {
        capturedPiece = (side == WHITE) ? BPAWN : WPAWN;
    } else {
        capturedPiece = piece_board[to_sq];
    }
    if (capturedPiece == EMPTY) return false;

    // Quick early exit: if we capture more than we can lose + threshold, it's good
    int swap = PIECE_VALUE[capturedPiece] - threshold;
    if (swap < 0) return false;  // Even capturing the piece doesn't meet threshold
    
    // If we can lose our piece but still meet threshold, it's good
    swap = PIECE_VALUE[movingPiece] - swap;
    if (swap <= 0) return true;  // Even if we lose our piece, we still profit enough
    
    // Need full SEE calculation
    U64 occ = pieces();
    occ ^= (1ULL << from_sq);
    
    if (moveIsEnPassant(m)) {
        int ep_sq = to_sq + (side == WHITE ? -8 : 8);
        occ ^= (1ULL << ep_sq);
    }
    
    // Get all attackers
    U64 allAttackers = allAttackersTo(to_sq, occ, bitboards);
    allAttackers &= occ;
    allAttackers &= ~(1ULL << from_sq);
    
    U64 whitePieces = color_bitboards[WHITE] & occ;
    U64 blackPieces = color_bitboards[BLACK] & occ;
    
    int stm = side ^ 1;
    int result = 1;  // 1 = true, 0 = false
    
    while (true) {
        stm ^= 1;
        allAttackers &= occ;
        
        U64 stmPieces = (stm == WHITE) ? whitePieces : blackPieces;
        U64 stmAttackers = allAttackers & stmPieces;
        
        if (!stmAttackers) break;
        
        result ^= 1;
        
        // Find least valuable attacker and update swap
        static const int pieceOrder[2][6] = {
            {WPAWN, WKNIGHT, WBISHOP, WROOK, WQUEEN, WKING},
            {BPAWN, BKNIGHT, BBISHOP, BROOK, BQUEEN, BKING}
        };
        
        U64 bb = 0;
        int attackerPiece = EMPTY;
        
        for (int i = 0; i < 6; i++) {
            int pt = pieceOrder[stm][i];
            bb = bitboards[pt] & stmAttackers;
            if (bb) {
                attackerPiece = pt;
                swap = PIECE_VALUE[pt] - swap;
                if (swap < result) break;  // Early exit!
                break;
            }
        }
        
        if (!bb) break;
        
        // Check for early exit
        if (swap < result) break;
        
        // Remove attacker from occupancy
        int attackerSq = lsb_index(bb);
        occ ^= (1ULL << attackerSq);
        
        // Update X-ray attackers
        if (attackerPiece == WPAWN || attackerPiece == BPAWN ||
            attackerPiece == WBISHOP || attackerPiece == BBISHOP ||
            attackerPiece == WQUEEN || attackerPiece == BQUEEN) {
            U64 diag = bitboards[WBISHOP] | bitboards[WQUEEN] | bitboards[BBISHOP] | bitboards[BQUEEN];
            allAttackers |= Magic::bishopAttacks(to_sq, occ) & diag;
        }
        if (attackerPiece == WROOK || attackerPiece == BROOK ||
            attackerPiece == WQUEEN || attackerPiece == BQUEEN) {
            U64 straight = bitboards[WROOK] | bitboards[WQUEEN] | bitboards[BROOK] | bitboards[BQUEEN];
            allAttackers |= Magic::rookAttacks(to_sq, occ) & straight;
        }
        
        if (stm == WHITE) whitePieces ^= (1ULL << attackerSq);
        else blackPieces ^= (1ULL << attackerSq);
    }
    
    return bool(result);
}

// ========================= Has Non-Pawn Material =========================
bool Position::hasNonPawnMaterial(bool white) const {
    if (white) {
        return (bitboards[WKNIGHT] | bitboards[WBISHOP] | 
                bitboards[WROOK] | bitboards[WQUEEN]) != 0;
    } else {
        return (bitboards[BKNIGHT] | bitboards[BBISHOP] | 
                bitboards[BROOK] | bitboards[BQUEEN]) != 0;
    }
}
