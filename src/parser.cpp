/**
 * This file is part of Open Chess Game Database Standard.
 *
 * Copyright (c) 2021-2022 Nguyen Pham (github@nguyenpham)
 * Copyright (c) 2021-2022 Developers
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include <stdio.h>
#include <string.h>
#include <iostream>
#include <sstream>
#include <map>
#include <set>
#include <array>
#include <algorithm>

#include "parser.h"
#include "board/chess.h"
#include "board/base.h"

static int popCount(uint64_t x)
{
   int count = 0;
   while (x) {
       count++;
       x &= x - 1; // reset LS1B
   }
   return count;
}

using namespace ocgdb;

static const std::unordered_map<std::string, Operator> string2operatorMap{
    {"and", Operator::op_and},
    {"&&", Operator::op_and},
    {"or", Operator::op_or},
    {"||", Operator::op_or},
    {"+", Operator::op_add},
    {"-", Operator::op_sub },
    {"*", Operator::op_multi},
    {"/", Operator::op_div},

    {"=", Operator::op_eq},
    {"==", Operator::op_eq},
    {"<", Operator::op_l},
    {"<=", Operator::op_le},
    {">", Operator::op_g},
    {">=", Operator::op_ge},
    {"<>", Operator::op_ne},
    {"!=", Operator::op_ne},
};

static Operator string2operator(const std::string& s)
{
    auto it = string2operatorMap.find(s);
    return it != string2operatorMap.end() ? it->second : Operator::none;
}

Node::Node(const LexWord& w)
{
    assert(w.lex < Lex::no_node);
    string = w.string;
    if (w.lex == Lex::number) {
        nodeType = NodeType::number;
        number = atoi(w.string.c_str());
    } else if (w.lex >= Lex::operator_begin) {
        nodeType = NodeType::op;
        op = string2operator(string);
    }
}


static const std::string noteTypeStrings[] = {
    "none", "piece", "number", "op"
};

std::string Node::toString() const
{
    std::string s =
        noteTypeStrings[static_cast<int>(nodeType)]
        + " " + string
        ;
    
    if (nodeType == NodeType::piece && hassquareset) {
        s += "\n" + bslib::ChessBoard::bitboard2string(squareset);
    }
    
    return s;
}

static int coordinate2pos(const char* s)
{
    auto c = s[0], d = s[1];
    if (c >= 'a' && c <= 'h' && d >= '1' && d <= '8') {
        auto pos = static_cast<int>('8' - d) * 8 + static_cast<int>(c - 'a');
        if (pos >= 0 && pos < 64) {
            return pos;
        }
    }
    
    return -1;
}

int Node::selectSquare(const char* s)
{
    if (!s || !*s) return 0;
    
    auto len = strlen(s);
    if (len > 2) return -1;
    
    // column
    if (isalpha(*s)) {
        // row -> square
        if (isdigit(*(s + 1))) {
            auto pos = coordinate2pos(s);
            if (pos >= 0) {
                squareset |= bslib::ChessBoard::_posToBitboard[pos];
                hassquareset = true;
                return 1;
            }
            return -1;
        }
        
        if (len == 1) {
            auto c = static_cast<int>(*s - 'a');
            if (c >= 0 && c < 8) {
                
                for(auto i = 0; i < 8; ++i) {
                    auto x = i * 8 + c;
                    squareset |= bslib::ChessBoard::_posToBitboard[x];
                }
                hassquareset = true;
                return 1;
            }
        }
        
        return -1;
    }

    // row
    if (*s >= '1' && *s <= '8') {
        auto r = static_cast<int>('8' - *s) * 8;

        for(auto i = 0; i < 8; ++i) {
            auto x = r + i;
            squareset |= bslib::ChessBoard::_posToBitboard[x];
        }
        hassquareset = true;
        return 1;
    }

    return -1;
}

int Node::selectSquare(const std::string& from, const std::string& to)
{
    assert(!from.empty() && !to.empty());
    
    if (from.size() == 2 && to.size() == 2) {
        auto fromPos = coordinate2pos(from.c_str());
        auto toPos = coordinate2pos(to.c_str());
        if (fromPos >= 0 && toPos >= 0 && fromPos != toPos) {
            if (fromPos > toPos) {
                std::swap(fromPos, toPos);
            }
            
            for(auto i = fromPos; i <= toPos; ++i) {
                squareset |= bslib::ChessBoard::_posToBitboard[i];
            }
            hassquareset = true;
            return 1;
        }
        return -1;
    }

    if (from.size() == 1 && to.size() == 1) {
        auto fch = from[0], tch = to[0];

        // column
        if (isalpha(fch) && isalpha(tch)) {
            if (fch > tch) std::swap(fch, tch);
            
            for(auto t = fch; t <= tch; ++t) {
                auto c = static_cast<int>(t - 'a');
                for(auto i = 0; i < 8; ++i) {
                    auto x = i * 8 + c;
                    squareset |= bslib::ChessBoard::_posToBitboard[x];
                }
            }
            hassquareset = true;
            return 1;
        }

        if (isdigit(fch) && isdigit(tch)) {
            if (fch > tch) std::swap(fch, tch);
            
            for(auto t = fch; t <= tch; ++t) {
                auto r = static_cast<int>('8' - t) * 8;
                for(auto i = 0; i < 8; ++i) {
                    auto x = r + i;
                    squareset |= bslib::ChessBoard::_posToBitboard[x];
                }
            }
            hassquareset = true;
            return 1;
        }
    }
    
    return -1;
}


bool Node::isValid() const
{
    switch (nodeType) {
        case NodeType::fen:
            return !fenHashSet.empty();

        case NodeType::op:
            return lhs && rhs && lhs->isValid() && rhs->isValid() && op < Operator::none;
            
        case NodeType::piece:
            return !lhs && !rhs;
            
        case NodeType::number:
            return !lhs && !rhs;

        case NodeType::pattern:
            return !patternBitBoards.empty() && patternTolerance >= 0;

        case NodeType::meta:
            return !lhs && !rhs && metaField != MetaField::none;

        case NodeType::stringlit:
            return !lhs && !rhs;

        default:
            break;
    }

    return false;
}

int Node::evaluate(const std::vector<uint64_t>& bitboardVec) const
{
    switch (nodeType) {
        case NodeType::fen:
        {
            assert(!fenHashSet.empty());
            auto hash = bitboardVec[static_cast<int>(bslib::BBIdx::hash)];
            return isInFenHashSet(hash) ? 1 : 0;
        }
        case NodeType::op:
        {
            assert(lhs && rhs);
            auto l = lhs->evaluate(bitboardVec), r = rhs->evaluate(bitboardVec);
            switch (op) {
                case Operator::op_and:
                    return (l && r) ? 1 : 0;
                case Operator::op_or:
                    return (l || r) ? 1 : 0;
                case Operator::op_add:
                    return l + r;
                case Operator::op_sub:
                    return l - r;
                case Operator::op_multi:
                    return l * r;
                case Operator::op_div:
                    return r != 0 ? l / r : 0; /// ?

                case Operator::op_eq:
                    return l == r ? 1 : 0;
                case Operator::op_l:
                    return l < r ? 1 : 0;
                case Operator::op_le:
                    return l <= r ? 1 : 0;
                case Operator::op_g:
                    return l > r ? 1 : 0;
                case Operator::op_ge:
                    return l >= r ? 1 : 0;
                case Operator::op_ne:
                    return l != r ? 1 : 0;

                default:
                    break;
            }
            break;
        }
            
        case NodeType::piece:
            assert(!lhs && !rhs);
            
            int64_t bb;
            switch (string.at(0)) {
                case 'w':
                    assert(string == "white");
                    bb = bitboardVec[static_cast<int>(bslib::BBIdx::white)];
                    break;

                case 'K':
                    bb = bitboardVec[static_cast<int>(bslib::BBIdx::white)] & bitboardVec[static_cast<int>(bslib::BBIdx::kings)];
                    break;

                case 'Q':
                    bb = bitboardVec[static_cast<int>(bslib::BBIdx::white)] & bitboardVec[static_cast<int>(bslib::BBIdx::queens)];
                    break;
                case 'R':
                    bb = bitboardVec[static_cast<int>(bslib::BBIdx::white)] & bitboardVec[static_cast<int>(bslib::BBIdx::rooks)];
                    break;

                case 'B':
                    bb = bitboardVec[static_cast<int>(bslib::BBIdx::white)] & bitboardVec[static_cast<int>(bslib::BBIdx::bishops)];
                    break;
                case 'N':
                    bb = bitboardVec[static_cast<int>(bslib::BBIdx::white)] & bitboardVec[static_cast<int>(bslib::BBIdx::knights)];
                    break;
                case 'P':
                    bb = bitboardVec[static_cast<int>(bslib::BBIdx::white)] & bitboardVec[static_cast<int>(bslib::BBIdx::pawns)];
                    break;

                case 'k':
                    bb = bitboardVec[static_cast<int>(bslib::BBIdx::black)] & bitboardVec[static_cast<int>(bslib::BBIdx::kings)];
                    break;

                case 'q':
                    bb = bitboardVec[static_cast<int>(bslib::BBIdx::black)] & bitboardVec[static_cast<int>(bslib::BBIdx::queens)];
                    break;
                    
                case 'r':
                    bb = bitboardVec[static_cast<int>(bslib::BBIdx::black)] & bitboardVec[static_cast<int>(bslib::BBIdx::rooks)];
                    break;

                case 'b':
                    bb = bitboardVec[static_cast<int>(bslib::BBIdx::black)];
                    if (string == "b") {
                        bb &= bitboardVec[static_cast<int>(bslib::BBIdx::bishops)];
                    } else {
                        assert(string == "black");
                    }
                    break;
                case 'n':
                    bb = bitboardVec[static_cast<int>(bslib::BBIdx::black)] & bitboardVec[static_cast<int>(bslib::BBIdx::knights)];
                    break;
                case 'p':
                    bb = bitboardVec[static_cast<int>(bslib::BBIdx::black)] & bitboardVec[static_cast<int>(bslib::BBIdx::pawns)];
                    break;

                default:
                    bb = 0;
                    break;
            }
            
            if (hassquareset) {
                bb &= squareset;
            }
            return popCount(bb);
            
        case NodeType::number:
            assert(number == std::atoi(string.c_str()));
            return number;
            
        case NodeType::pattern:
        {
            assert(!patternBitBoards.empty());
            auto i = 0;
            for(auto && patternVec : patternBitBoards) {
                if (patternOperand == PatternOperand::greaterthan) {
                    if (evaluate_pattern(bitboardVec, patternVec, PatternOperand::lessthan, patternTolerance)) {
                        return 1;
                    }
                }
                if (evaluate_pattern(patternVec, bitboardVec, patternOperand, patternTolerance)) {
                    return 1;
                }
                i++;
            }
            break;
        }

        default:
            break;
    }
    
    return 0;
}

bool Node::evaluate_pattern(const std::vector<uint64_t>& bbSubVec, const std::vector<uint64_t>& bbSuperVec, PatternOperand operand, int tolerance) const
{
    auto blackSb = bbSubVec[static_cast<int>(bslib::BBIdx::black)], whiteSb = bbSubVec[static_cast<int>(bslib::BBIdx::white)];
    auto blackSp = bbSuperVec[static_cast<int>(bslib::BBIdx::black)], whiteSp = bbSuperVec[static_cast<int>(bslib::BBIdx::white)];

    uint64_t blackDif, whiteDif;

    if (operand == PatternOperand::equal) {
        blackDif = (blackSb | blackSp) & ~(blackSb & blackSp);
        whiteDif = (whiteSb | whiteSp) & ~(whiteSb & whiteSp);
    } else {
        blackDif = blackSb & ~(blackSb & blackSp);
        whiteDif = whiteSb & ~(whiteSb & whiteSp);
    }

    auto cnt = popCount(blackDif | whiteDif);

    if (cnt > tolerance) return false;

    // check further, each position
    uint64_t d = 0;
    for(auto idx = static_cast<int>(bslib::BBIdx::kings); idx < static_cast<int>(bslib::BBIdx::max); ++idx) {
        auto sb = bbSubVec[idx], sp = bbSuperVec[idx];

        uint64_t dif;

        if (operand == PatternOperand::equal) {
            dif = (sb | sp) & ~(sb & sp);
        } else {
            dif = sb & ~(sb & sp);
        }

        d |= dif;
    }

    return popCount(d) <= tolerance;
}

void Node::pattern_shift()
{
    assert(patternBitBoards.size() == 1);

    auto v = patternBitBoards.at(0);

    while (pattern_shift_up(v)) {
        patternBitBoards.push_back(v);
        auto v2 = v;
        while (pattern_shift_left(v2)) {
            patternBitBoards.push_back(v2);
        }
        v2 = v;
        while (pattern_shift_right(v2)) {
            patternBitBoards.push_back(v2);
        }
    }

    v = patternBitBoards.at(0);
    while (pattern_shift_down(v)) {
        patternBitBoards.push_back(v);
        auto v2 = v;
        while (pattern_shift_left(v2)) {
            patternBitBoards.push_back(v2);
        }
        v2 = v;
        while (pattern_shift_right(v2)) {
            patternBitBoards.push_back(v2);
        }
    }
}

bool Node::pattern_shift_up(std::vector<uint64_t>& v)
{
    assert(!v.empty());
    auto bb = v[static_cast<int>(bslib::BBIdx::black)] | v[static_cast<int>(bslib::BBIdx::white)];
    assert(bb);

    if (bb & bslib::ChessBoard::bb_edge_top)
        return false;

    for(int i = static_cast<int>(bslib::BBIdx::black); i < static_cast<int>(bslib::BBIdx::max); i++) {
        v[i] <<= 8;

        // Pawn can't go to the top line
        if (i == static_cast<int>(bslib::BBIdx::pawns) && (v[i] & bslib::ChessBoard::bb_edge_top)) {
            return false;
        }
    }

    v[static_cast<int>(bslib::BBIdx::blackkingsquare)] += 8;
    v[static_cast<int>(bslib::BBIdx::whitekingsquare)] += 8;

    return true;
}

bool Node::pattern_shift_down(std::vector<uint64_t>& v)
{
    assert(!v.empty());
    auto bb = v[static_cast<int>(bslib::BBIdx::black)] | v[static_cast<int>(bslib::BBIdx::white)];
    assert(bb);

    if (bb & bslib::ChessBoard::bb_edge_bottom)
        return false;

    for(int i = static_cast<int>(bslib::BBIdx::black); i < static_cast<int>(bslib::BBIdx::max); i++) {
        v[i] >>= 8;

        // Pawn can't go to the bottom line
        if (i == static_cast<int>(bslib::BBIdx::pawns) && (v[i] & bslib::ChessBoard::bb_edge_bottom)) {
            return false;
        }
    }

    v[static_cast<int>(bslib::BBIdx::blackkingsquare)] -= 8;
    v[static_cast<int>(bslib::BBIdx::whitekingsquare)] -= 8;

    return true;
}

bool Node::pattern_shift_left(std::vector<uint64_t>& v)
{
    assert(!v.empty());
    auto bb = v[static_cast<int>(bslib::BBIdx::black)] | v[static_cast<int>(bslib::BBIdx::white)];
    assert(bb);

    if (bb & bslib::ChessBoard::bb_edge_left)
        return false;

    for(int i = static_cast<int>(bslib::BBIdx::black); i < static_cast<int>(bslib::BBIdx::max); i++) {
        v[i] >>= 1;
    }

    v[static_cast<int>(bslib::BBIdx::blackkingsquare)]--;
    v[static_cast<int>(bslib::BBIdx::whitekingsquare)]--;
    return true;
}

bool Node::pattern_shift_right(std::vector<uint64_t>& v)
{
    assert(!v.empty());
    auto bb = v[static_cast<int>(bslib::BBIdx::black)] | v[static_cast<int>(bslib::BBIdx::white)];
    assert(bb);

    if (bb & bslib::ChessBoard::bb_edge_right)
        return false;

    for(int i = static_cast<int>(bslib::BBIdx::black); i < static_cast<int>(bslib::BBIdx::max); i++) {
        v[i] <<= 1;
    }

    v[static_cast<int>(bslib::BBIdx::blackkingsquare)]++;
    v[static_cast<int>(bslib::BBIdx::whitekingsquare)]++;
    return true;
}

////////////////////////////////////
Parser::Parser()
{
}

Parser::~Parser()
{
    deleteTree();

    if (board) delete board;
    if (board2) delete board2;
}

void Parser::deleteTree()
{
    deleteTree(root);
    root = nullptr;
}

void Parser::deleteTree(Node* node) const
{
    if (node) {
        deleteTree(node->lhs);
        deleteTree(node->rhs);
        delete node;
    }
}

static const std::string errorStrings[] = {
    "none",
    "no input",
    "wrong lexical",
    "missing condition",
    "missing comparison",
    "missing term",
    "missing factor",
    "missing close bracket",
    "input invalid",
    "invalid use of a metadata term (whiteplayer/blackplayer/player/event/site/welo/belo/elo/"
    "date/year/eco/result) -- these can only be combined with \"and\" (not \"or\" or nested "
    "inside another expression), and text fields only support \"=\"/\"<>\"",
};

std::string Parser::getErrorString(ParseError error)
{
    if (error >= ParseError::max) return "unknown";
    return errorStrings[static_cast<int>(error)];
}

std::string Parser::getErrorString() const
{
    return getErrorString(error);
}

void Parser::printError() const
{
    std::cerr << getErrorString() << std::endl;
}

void Parser::printTree() const
{
    printTree(root, "");
}

void Parser::printTree(const Node* node, std::string prefix) const
{
    if (!node) return;
    
    std::cout << prefix << node->toString() << std::endl;
    
    prefix += "   ";
    printTree(node->lhs, prefix);
    printTree(node->rhs, prefix);
}

int Parser::evaluate(const std::vector<uint64_t>& bitboardVec) const
{
    // A null root means the whole query was metadata terms, fully
    // absorbed by extractMetadata() during parse() -- every position of
    // a game that already passed the SQL WHERE clause counts as a match
    // (there's no position-level constraint left to check).
    return !root || root->evaluate(bitboardVec);
}

namespace {

// Must stay in sync with the CREATE TABLE column list in material.cpp
// (MaterialBuilder::openDB()).
struct PieceCol { char letter; const char* column; };
const PieceCol kPieceCols[12] = {
    {'K', "MaxKw"}, {'Q', "MaxQw"}, {'R', "MaxRw"}, {'B', "MaxBw"}, {'N', "MaxNw"}, {'P', "MaxPw"},
    {'k', "MaxKb"}, {'q', "MaxQb"}, {'r', "MaxRb"}, {'b', "MaxBb"}, {'n', "MaxNb"}, {'p', "MaxPb"},
};

// Index into kPieceCols for a bare single-letter piece node (K/Q/R/B/N/P/
// k/q/r/b/n/p), ignoring any square mask -- a masked count is always <=
// the unmasked total, so a bound derived from a masked comparison still
// safely bounds the unmasked total. Returns -1 for anything else,
// including the "white"/"black" total pseudo-pieces (no column for those).
int pieceColIndex(const Node* node)
{
    if (!node || node->nodeType != NodeType::piece || node->string.size() != 1) return -1;
    auto c = node->string[0];
    for (int i = 0; i < 12; i++) {
        if (kPieceCols[i].letter == c) return i;
    }
    return -1;
}

// Safe lower bound on a piece's count implied by "piece op constant"
// evaluating truthy (nonzero), after normalizing so the piece is
// conceptually on the left (flipping the operator if it was actually on
// the right -- "3 <= Q" means the same thing as "Q >= 3"). 0 means "no
// bound extractable", which is always a safe (if unhelpful) answer.
int boundFromComparison(Operator op, bool pieceOnLeft, int constant)
{
    auto normOp = op;
    if (!pieceOnLeft) {
        switch (op) {
            case Operator::op_ge: normOp = Operator::op_le; break;
            case Operator::op_g:  normOp = Operator::op_l;  break;
            case Operator::op_le: normOp = Operator::op_ge; break;
            case Operator::op_l:  normOp = Operator::op_g;  break;
            default: break; // = and <> are symmetric already
        }
    }
    switch (normOp) {
        case Operator::op_eq: return constant > 0 ? constant : 0;
        case Operator::op_ge: return constant > 0 ? constant : 0;
        case Operator::op_g:  return constant >= 0 ? constant + 1 : 0;
        default: return 0; // <, <=, <> can all be satisfied by a count of 0
    }
}

// Fills `bounds[0..11]` with a safe lower bound per piece column implied
// by `node` evaluating truthy. See buildMaterialPreFilterSql() (parser.h)
// for the reasoning behind and/or combination and why anything else
// (arithmetic, piece-vs-piece, fen/pattern) is left at 0.
void collectMaterialBounds(const Node* node, std::array<int, 12>& bounds)
{
    if (!node) return;

    if (node->nodeType == NodeType::piece) {
        auto idx = pieceColIndex(node);
        if (idx >= 0) bounds[idx] = std::max(bounds[idx], 1);
        return;
    }

    if (node->nodeType != NodeType::op) return;

    if (node->op == Operator::op_and || node->op == Operator::op_or) {
        std::array<int, 12> l{}, r{};
        collectMaterialBounds(node->lhs, l);
        collectMaterialBounds(node->rhs, r);
        auto isAnd = node->op == Operator::op_and;
        for (int i = 0; i < 12; i++) {
            bounds[i] = std::max(bounds[i], isAnd ? std::max(l[i], r[i]) : std::min(l[i], r[i]));
        }
        return;
    }

    auto lIdx = pieceColIndex(node->lhs);
    auto rIdx = pieceColIndex(node->rhs);
    auto lNum = node->lhs && node->lhs->nodeType == NodeType::number;
    auto rNum = node->rhs && node->rhs->nodeType == NodeType::number;

    if (lIdx >= 0 && rNum) {
        bounds[lIdx] = std::max(bounds[lIdx], boundFromComparison(node->op, true, node->rhs->number));
    } else if (rIdx >= 0 && lNum) {
        bounds[rIdx] = std::max(bounds[rIdx], boundFromComparison(node->op, false, node->lhs->number));
    }
    // Anything else (arithmetic combinations like "Q+R", comparisons
    // between two pieces, fen/pattern clauses) yields no extra info here
    // -- a safe no-op, not traversed any further.
}

} // namespace

std::string Parser::buildMaterialPreFilterSql() const
{
    if (!root) return {};

    std::array<int, 12> bounds{};
    collectMaterialBounds(root, bounds);

    std::string sql;
    for (int i = 0; i < 12; i++) {
        if (bounds[i] <= 0) continue;
        if (!sql.empty()) sql += " AND ";
        sql += std::string(kPieceCols[i].column) + " >= " + std::to_string(bounds[i]);
    }
    return sql;
}

namespace {

// Standard SQL string-literal escaping (double up embedded single quotes)
// -- the complete, sufficient defense against SQL injection for a value
// embedded as a quoted literal; no other character needs special handling
// in a SQLite string literal.
std::string sqlQuoteString(const std::string& s)
{
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += '\'';
        out += c;
    }
    out += "'";
    return out;
}

// PQL wildcard syntax ("*"/"?", familiar from shell globs and how
// /api/players etc. already document prefix search) -> SQL LIKE syntax
// ("%"/"_"). Any literal %, _, or \ the user actually typed is escaped
// with a backslash so it can't be mistaken for a wildcard; paired with
// "ESCAPE '\'" at every call site that uses this.
std::string globToLikePattern(const std::string& s)
{
    std::string out;
    for (char c : s) {
        if (c == '*') out += '%';
        else if (c == '?') out += '_';
        else if (c == '%' || c == '_' || c == '\\') { out += '\\'; out += c; }
        else out += c;
    }
    return out;
}

bool metaFieldNeedsNames(MetaField f)
{
    return f == MetaField::whitePlayer || f == MetaField::blackPlayer || f == MetaField::player;
}

// Text fields go through LIKE/NOT LIKE with glob-wildcard translation
// (see globToLikePattern) and only support "="/"<>" -- comparisons like
// "<" on a player name don't have a sound meaning, so they're rejected at
// the call site rather than silently doing a lexicographic string compare
// nobody asked for. Numeric fields (welo/belo/elo/year) support all six
// operators directly.
bool metaFieldIsText(MetaField f)
{
    switch (f) {
        case MetaField::whitePlayer: case MetaField::blackPlayer: case MetaField::player:
        case MetaField::event: case MetaField::site:
        case MetaField::date: case MetaField::eco: case MetaField::result:
            return true;
        default:
            return false;
    }
}

const char* opToSql(Operator op)
{
    switch (op) {
        case Operator::op_eq: return "=";
        case Operator::op_l:  return "<";
        case Operator::op_le: return "<=";
        case Operator::op_g:  return ">";
        case Operator::op_ge: return ">=";
        case Operator::op_ne: return "<>";
        default: return nullptr;
    }
}

// Builds the SQL fragment for one "metaField op constant" comparison
// (constant already normalized to the metadata side via `pieceOnLeft`,
// same trick as boundFromComparison() above). Returns empty (and sets
// *ok = false) for anything not soundly expressible: an operator with no
// meaning for the field's type, or (defensively) an unrecognized field.
std::string metaComparisonSql(MetaField field, Operator op, bool metaOnLeft,
                               int numConstant, const std::string& strConstant, bool isNum, bool& ok)
{
    // Normalize so the field is conceptually on the left, same as
    // boundFromComparison() -- "2700 <= welo" means the same as
    // "welo >= 2700".
    auto normOp = op;
    if (!metaOnLeft) {
        switch (op) {
            case Operator::op_ge: normOp = Operator::op_le; break;
            case Operator::op_g:  normOp = Operator::op_l;  break;
            case Operator::op_le: normOp = Operator::op_ge; break;
            case Operator::op_l:  normOp = Operator::op_g;  break;
            default: break; // = and <> are symmetric already
        }
    }

    if (metaFieldIsText(field)) {
        if (!isNum && (normOp == Operator::op_eq || normOp == Operator::op_ne)) {
            auto likeOp = normOp == Operator::op_eq ? "LIKE" : "NOT LIKE";
            auto pattern = sqlQuoteString(globToLikePattern(strConstant)) + " ESCAPE '\\'";
            switch (field) {
                case MetaField::whitePlayer: return "White " + std::string(likeOp) + " " + pattern;
                case MetaField::blackPlayer: return "Black " + std::string(likeOp) + " " + pattern;
                case MetaField::player:
                    return "(White " + std::string(likeOp) + " " + pattern + " OR Black " + std::string(likeOp) + " " + pattern + ")";
                case MetaField::event: return "Event " + std::string(likeOp) + " " + pattern;
                case MetaField::site:  return "Site "  + std::string(likeOp) + " " + pattern;
                case MetaField::eco:   return "ECO "   + std::string(likeOp) + " " + pattern;
                case MetaField::result: return "Result " + std::string(likeOp) + " " + pattern;
                case MetaField::date:  return "Date "  + std::string(likeOp) + " " + pattern;
                default: break;
            }
        }
        ok = false;
        return {};
    }

    // Numeric field: welo/belo/elo/year.
    auto sqlOp = opToSql(normOp);
    if (!sqlOp || isNum == false) {
        ok = false;
        return {};
    }
    auto n = std::to_string(numConstant);
    switch (field) {
        case MetaField::welo: return "WhiteElo " + std::string(sqlOp) + " " + n;
        case MetaField::belo: return "BlackElo " + std::string(sqlOp) + " " + n;
        case MetaField::elo:  return "(WhiteElo " + std::string(sqlOp) + " " + n + " OR BlackElo " + std::string(sqlOp) + " " + n + ")";
        case MetaField::year: return "CAST(SUBSTR(Date,1,4) AS INTEGER) " + std::string(sqlOp) + " " + n;
        default: break;
    }
    ok = false;
    return {};
}

// Final safety net after extractMetadata() runs: that function only ever
// *descends* into "and" chains (deliberately -- see its own comment), so
// a metadata term sitting anywhere else in the tree (inside "or", as an
// arithmetic operand, anywhere extractMetadata() didn't specifically
// recurse into) is left completely untouched rather than examined at
// all. Node::evaluate() has no per-game data to give such a node -- it
// would silently fall through to evaluate()'s NodeType default case and
// return 0, quietly turning e.g. "Q=3 or welo>=2700" into just "Q=3".
// This scans the *entire* final tree regardless of extractMetadata()'s
// own recursion shape, so parse() can refuse the query outright instead.
bool containsMetaNode(const Node* node)
{
    if (!node) return false;
    if (node->nodeType == NodeType::meta) return true;
    return containsMetaNode(node->lhs) || containsMetaNode(node->rhs);
}

} // namespace

Node* Parser::extractMetadata(Node* node, bool& ok)
{
    if (!node || !ok) return node;

    if (node->nodeType == NodeType::meta) {
        // A bare metadata field with no comparison (e.g. just "welo" on
        // its own) never reaches here -- Node::isValid() (see above)
        // already rejects it before extraction runs, same as a bare
        // number would be meaningless standing alone.
        ok = false;
        return node;
    }

    if (node->nodeType == NodeType::op && node->op == Operator::op_and) {
        node->lhs = extractMetadata(node->lhs, ok);
        node->rhs = extractMetadata(node->rhs, ok);
        if (!ok) return node;

        if (!node->lhs && !node->rhs) { delete node; return nullptr; }
        if (!node->lhs) { auto r = node->rhs; node->rhs = nullptr; delete node; return r; }
        if (!node->rhs) { auto l = node->lhs; node->lhs = nullptr; delete node; return l; }
        return node;
    }

    if (node->nodeType == NodeType::op) {
        auto lIsMeta = node->lhs && node->lhs->nodeType == NodeType::meta;
        auto rIsMeta = node->rhs && node->rhs->nodeType == NodeType::meta;

        if (lIsMeta || rIsMeta) {
            // A metadata comparison anywhere except directly under a
            // chain of "and"s -- inside "or", or as an operand of
            // arithmetic -- can't be pulled out without changing what
            // the query means (see getMetadataWhereSql() in parser.h).
            // Reject outright rather than silently doing something
            // unsound; NOT extracting it and leaving it for
            // Node::evaluate() would be equally wrong, since evaluate()
            // has no per-game metadata to look at.
            if (node->op != Operator::op_eq && node->op != Operator::op_ne &&
                node->op != Operator::op_l && node->op != Operator::op_le &&
                node->op != Operator::op_g && node->op != Operator::op_ge) {
                ok = false;
                return node;
            }

            auto metaNode = lIsMeta ? node->lhs : node->rhs;
            auto otherNode = lIsMeta ? node->rhs : node->lhs;

            bool isNum = otherNode && otherNode->nodeType == NodeType::number;
            bool isStr = otherNode && otherNode->nodeType == NodeType::stringlit;
            if (!isNum && !isStr) {
                ok = false;
                return node;
            }

            auto sql = metaComparisonSql(metaNode->metaField, node->op, lIsMeta,
                                          isNum ? otherNode->number : 0,
                                          isStr ? otherNode->string : std::string(),
                                          isNum, ok);
            if (!ok) return node;

            if (!metadataWhereSql.empty()) metadataWhereSql += " AND ";
            metadataWhereSql += sql;
            if (metaFieldNeedsNames(metaNode->metaField)) metadataNeedsNames_ = true;

            deleteTree(node);
            return nullptr;
        }
    }

    // Anything else (a position-only comparison, "or", arithmetic,
    // fen/pattern) is left exactly as-is; extractMetadata() only ever
    // descends into "and" chains, by design (see the op_and branch
    // above) -- it does not recurse into node->lhs/node->rhs here.
    return node;
}

std::string Parser::stripLineComments(const std::string& query)
{
    // Logic moved here from search.cpp's CLI query loop (see runTask()
    // there), unchanged, so both the CLI and -server's /api/query strip
    // "//" comments the same way instead of only the CLI doing it.
    auto s = query;
    while (true) {
        auto p = s.find("//");
        if (p == std::string::npos) {
            break;
        }

        auto q = p + 2;
        for (; q < s.size(); q++) {
            auto ch = s.at(q);
            if (ch == '\n') {
                q++;
                break;
            }
        }

        auto s0 = s.substr(0, p);
        if (q >= s.size()) {
            s = s0;
            break;
        } else {
            auto s1 = s.substr(q);
            s = s0 + s1;
        }
    }
    return s;
}

bool Parser::parse(bslib::ChessVariant _variant, const char* s)
{
    assert(s);
    deleteTree();
    error = ParseError::none;
    variant = _variant;
    metadataWhereSql.clear();
    metadataNeedsNames_ = false;

    if (board) delete board;
    if (board2) delete board2;

    board = bslib::Funcs::createBoard(variant);
    board2 = bslib::Funcs::createBoard(variant);


    lexVec = lexParse(s);
    if (error == ParseError::none && lexVec.empty()) {
        error = ParseError::noinput;
    }
    if (error != ParseError::none) {
        return false;
    }

    Node* r = nullptr, *w = nullptr;
    
    // there is only one fen clause
    size_t from = 0;
    while (from < lexVec.size()) {
        auto node = parse_condition(from);
        if (!node) {
            node = parse_fenstring(from);
        }
        if (!node) {
            node = parse_pattern(from);
        }
        if (!node) {
            break;
        }
        
        auto stop = from >= lexVec.size();
        if (!stop) {
            auto lex = lexVec.at(from).lex;
            stop = lex != Lex::operator_and && lex != Lex::operator_or;
        }
        if (stop) {
            if (r) {
                assert(w && !w->rhs);
                w->rhs = node;
            } else {
                r = node;
            }
            break;
        }
        
        auto word = lexVec.at(from);
        assert(word.lex == Lex::operator_and || word.lex == Lex::operator_or);

        auto op = new Node(word);
        op->lhs = node;

        if (!r) {
            r = op;
        }
        if (w) {
            assert(!w->rhs);
            w->rhs = op;
        }
        w = op;
        ++from;
        if (from >= lexVec.size()) {
            // wrong
            error = ParseError::missing_condition;
            break;
        }
    }

    root = r;
    
    if (!root) {
        return false;
    }
    
    if (error == ParseError::none) {
        if (!root || !root->isValid()) {
            error = ParseError::invalid;
        }
    }

    if (error == ParseError::none) {
        auto metaOk = true;
        root = extractMetadata(root, metaOk);
        // Belt-and-suspenders: extractMetadata() only descends into "and"
        // chains, so confirm nothing metadata-shaped survived anywhere
        // else in the residual tree before trusting it to evaluate() --
        // see containsMetaNode()'s comment in the anonymous namespace
        // above for exactly what this catches (metadata inside "or", in
        // particular).
        if (metaOk && containsMetaNode(root)) {
            metaOk = false;
        }
        if (!metaOk) {
            error = ParseError::metadata_not_extractable;
        }
        // root may now be nullptr -- a query that was entirely metadata
        // terms (e.g. just "welo >= 2700"). evaluate() treats that as
        // "every position of a game that passed the SQL filter matches".
    }

    if (error != ParseError::none) {
        // Defensive: a rejected query (e.g. metadata term found inside an
        // "or", caught above after already having extracted an unrelated
        // sibling "and" clause) must not leave a partial WHERE fragment
        // sitting in metadataWhereSql for a caller to read -- every real
        // caller already checks parse()'s return value first and never
        // would, but getMetadataWhereSql()/metadataNeedsNames() shouldn't
        // depend on that discipline to be safe.
        metadataWhereSql.clear();
        metadataNeedsNames_ = false;
    }

    return error == ParseError::none;
}

//
Node* Parser::parse_fenclause(size_t& from)
{
    assert(from <= lexVec.size());
    if (lexVec.at(from).string != "fen") {
        return nullptr;
    }

    if (lexVec.size() < 4 || lexVec.at(from + 1).string != "[" || lexVec.at(from + 2).lex != Lex::fen) {
        error = ParseError::invalid;
        return nullptr;
    }
    
    auto node = new Node();
    node->nodeType = NodeType::fen;

    auto board = bslib::Funcs::createBoard(bslib::ChessVariant::standard);
    for(from += 2; from < lexVec.size(); ++from) {
        auto lex = lexVec.at(from);
        if (lex.lex == Lex::fen) {
            board->newGame(lex.string);
            node->fenHashSet.insert(board->hashKey);
            continue;
        }

        if (lex.string == ",") {
            continue;
        }
        if (lex.string == "]") {
            from++;
            break;
        }
    }

    delete board;
    return node;
}

Node* Parser::parse_fenstring(size_t& from)
{
    if (from < lexVec.size() && lexVec.at(from).lex == Lex::fen) {
        auto lex = lexVec.at(from);
        from++;

        board->newGame(lex.string);
        auto node = new Node();
        node->nodeType = NodeType::fen;
        node->fenHashSet.insert(board->hashKey);
        return node;
    }
    return nullptr;
}

const std::map<std::string, ocgdb::PatternOperand> patternOperandMap {
    {"=", PatternOperand::equal},
    { "==", PatternOperand::equal},
    { "<", PatternOperand::lessthan},
    { ">", PatternOperand::greaterthan},
    { "#", PatternOperand::shift}
};

Node* Parser::parse_pattern(size_t& from)
{
    assert(from <= lexVec.size());
    if (lexVec.at(from).string != "{") {
        return nullptr;
    }

    auto patternOperand = PatternOperand::equal;
    auto patternOperandDelta = 0;
    board->_clear();

    auto cnt = 0;
    for(++from; from < lexVec.size(); ++from) {
        auto lex = lexVec.at(from);
        if (lex.lex == Lex::fen) {
            board2->newGame(lex.string);

            for(auto i = 0; i < board2->size(); ++i) {
                auto piece = board2->getPiece(i);
                if (piece.isEmpty()) continue;
                board->_setPiece(i, piece);
            }
            continue;
        }

        if (lex.string == ",") {
            cnt++;
            continue;
        }
        if (lex.string == "}") {
            from++;
            break;
        }

        auto it = patternOperandMap.find(lex.string);
        if (it != patternOperandMap.end()) {
            patternOperand = it->second;
            continue;
        }

        auto node = parse_piece(from);
        if (node) {
            if (!node->locSet.empty() && node->pieceSide != bslib::Side::none) {
                bslib::Piece piece(node->pieceType, node->pieceSide);
                for(auto && sq : node->locSet) {
                    board->_setPiece(sq, piece);
                }
            }

            delete node;
            continue;
        }

        if (std::isdigit(lex.string.at(0))) {
            patternOperandDelta = std::stoi(lex.string);
            continue;
        }
    }

    if (board->_isEmpty()) {
        return nullptr;
    }

    auto node = new Node();
    node->nodeType = NodeType::pattern;
    node->patternBitBoards.push_back(board->posToBitboards());

    node->patternOperand = patternOperand;
    node->patternTolerance = patternOperandDelta;

    // shift
    if (patternOperand == PatternOperand::shift) {
        node->pattern_shift();
    }

    return node;
}

Node* Parser::parse_condition(size_t& from)
{
    assert(from <= lexVec.size());
    
    auto node = parse_fenclause(from);
    if (node) {
        return node;
    }
    
    if (error != ParseError::none) {
        return nullptr;
    }

    Node* r = nullptr, *w = nullptr;
    while (from < lexVec.size()) {
        auto node = parse_expression(from);
        if (!node) {
            break;
        }
        auto stop = from >= lexVec.size();
        if (!stop) {
            auto lex = lexVec.at(from).lex;
            stop = lex != Lex::operator_comparison;
        }
        if (stop) {
            if (r) {
                assert(w && !w->rhs);
                w->rhs = node;
            } else {
                r = node;
            }
            break;
        }
        
        auto word = lexVec.at(from);
        assert(word.lex == Lex::operator_comparison);
        
        auto op = new Node(word);
        op->lhs = node;

        if (!r) {
            r = op;
        }
        if (w) {
            assert(!w->rhs);
            w->rhs = op;
        }
        w = op;
        ++from;
        if (from >= lexVec.size()) {
            // wrong
            error = ParseError::missing_op_comparison;
            break;
        }
    }

    return r;
}

Node* Parser::parse_expression(size_t& from)
{
    assert(from <= lexVec.size());

    Node* r = nullptr, *w = nullptr;

    while (from < lexVec.size()) {
        auto node = parse_term(from);
        if (!node) {
            break;
        }
        
        auto stop = from >= lexVec.size();
        if (!stop) {
            auto lex = lexVec.at(from).lex;
            stop = lex != Lex::operator_add && lex != Lex::operator_sub;
        }
        if (stop) {
            if (r) {
                assert(w && !w->rhs);
                w->rhs = node;
            } else {
                r = node;
            }
            break;
        }

        auto word = lexVec.at(from);
        assert(word.lex == Lex::operator_add || word.lex == Lex::operator_sub);
        
        auto op = new Node(word);
        op->lhs = node;

        if (!r) {
            r = op;
        }
        if (w) {
            assert(!w->rhs);
            w->rhs = op;
        }
        w = op;

        ++from;
        if (from >= lexVec.size()) {
            // wrong
            error = ParseError::missing_term;
            break;
        }
    }

    return r;
}

Node* Parser::parse_term(size_t& from)
{
    assert(from <= lexVec.size());
    Node* r = nullptr, *w = nullptr;
    while (from < lexVec.size()) {
        auto node = parse_factor(from);
        if (!node) {
            break;
        }
        
        auto stop = from >= lexVec.size();
        if (!stop) {
            auto lex = lexVec.at(from).lex;
            stop = lex != Lex::operator_multi && lex != Lex::operator_div;
        }
        if (stop) {
            if (r) {
                assert(w && !w->rhs);
                w->rhs = node;
            } else {
                r = node;
            }
            break;
        }

        auto word = lexVec.at(from);
        assert(word.lex == Lex::operator_multi || word.lex == Lex::operator_div);

        auto op = new Node(word);
        op->lhs = node;

        if (!r) {
            r = op;
        }
        if (w) {
            assert(!w->rhs);
            w->rhs = op;
        }
        w = op;
        ++from;
        if (from >= lexVec.size()) {
            // wrong
            error = ParseError::missing_factor;
            break;
        }
    }

    return r;
}

Node* Parser::parse_factor(size_t& from)
{
    assert(from <= lexVec.size());

    auto word = lexVec.at(from);
    if (word.lex == Lex::number) {
        ++from;
        return new Node(word);
    }

    if (word.lex == Lex::stringlit) {
        ++from;
        auto node = new Node;
        node->nodeType = NodeType::stringlit;
        node->string = word.string;
        return node;
    }

    if (word.string == "(") {
        assert(word.lex == Lex::bracket);
        ++from;
        auto node = parse_expression(from);
        auto word2 = lexVec.at(from);
        if (word2.string == ")") {
            assert(word2.lex == Lex::bracket);
            from++;
            return node;
        } else {
            delete node;
            // wrong
            error = ParseError::missing_close;
        }
    }
    else
    if (word.lex == Lex::string) {
        // Metadata field names (welo, date, eco, ...) are a fixed,
        // disjoint keyword set from piece letters/"white"/"black" --
        // try them first, fall back to a piece/square token otherwise.
        auto metaNode = parse_metaname(from);
        if (metaNode) return metaNode;
        return parse_piece(from);
    }

    // wrong
    return nullptr;
}

namespace {
const std::unordered_map<std::string, MetaField> kMetaNameMap = {
    {"whiteplayer", MetaField::whitePlayer}, {"blackplayer", MetaField::blackPlayer},
    {"player", MetaField::player}, // either side
    {"event", MetaField::event}, {"site", MetaField::site},
    {"welo", MetaField::welo}, {"belo", MetaField::belo}, {"elo", MetaField::elo}, // elo: either side
    {"date", MetaField::date}, {"year", MetaField::year},
    {"eco", MetaField::eco}, {"result", MetaField::result},
};
} // namespace

Node* Parser::parse_metaname(size_t& from)
{
    assert(from <= lexVec.size());

    auto word = lexVec.at(from);
    if (word.lex != Lex::string) return nullptr;

    auto it = kMetaNameMap.find(word.string);
    if (it == kMetaNameMap.end()) return nullptr;

    auto node = new Node;
    node->nodeType = NodeType::meta;
    node->metaField = it->second;
    node->string = word.string;
    ++from;
    return node;
}

Node* Parser::parse_piece(size_t& from)
{
    assert(from <= lexVec.size());

    auto node = parse_piecename(from);
    if (!node) return nullptr;

    if (from < lexVec.size()) {
        parse_squareset(node, from);
    }
    return node;
}

void Parser::parse_squareset(Node* node, size_t& from)
{
    assert(node);
    auto word = lexVec.at(from);
    if (word.string != "[") {
        return;
    }
    assert(word.lex == Lex::bracket);
    ++from;
    while(from < lexVec.size()) {
        auto word = lexVec.at(from++);
        if (word.lex == Lex::comma) {
            continue;
        }
        if (word.string == "]") {
            assert(word.lex == Lex::bracket);
            return;
        }
        
        if (from + 1 < lexVec.size() && lexVec.at(from).string == "-") {
            from++;
            if (node->selectSquare(word.string, lexVec.at(from++).string) < 0) {
                error = ParseError::wrong_lexical;
                return;
            }
            continue;
        }
        if (word.lex == Lex::string || word.lex == Lex::number) {
            if (node->selectSquare(word.string.c_str()) < 0) {
                error = ParseError::wrong_lexical;
                return;
            }
        }
    }

    error = ParseError::missing_close;
}

// R rc b8
Node* Parser::parse_piecename(size_t& from)
{
    assert(from <= lexVec.size());

    auto word = lexVec.at(from);
    if (word.lex == Lex::string) {
        assert(!word.string.empty());
        auto ch = word.string.at(0);
        auto len = 1;
        if (strchr("KQRBNPkqrbnpw", ch)) {
            auto node = new Node;
            node->nodeType = NodeType::piece;
            node->string = ch;
            if (ch == 'w') {
                if (memcmp(word.string.c_str(), "white", 5) == 0) {
                    node->string = "white";
                    len = 5;
                }
            } else
            if (ch == 'b') {
                if (memcmp(word.string.c_str(), "black", 5) == 0) {
                    node->string = "black";
                    len = 5;
                }
            }
            
            if (len < word.string.size()) {
                if (node->selectSquare(word.string.c_str() + len) < 0) {
                    error = ParseError::wrong_lexical;
                }
            }

            ++from;
            return node;
        }
    }

    return nullptr;
}

std::vector<LexWord> Parser::lexParse(const char* s)
{
    assert(s && error == ParseError::none);

    enum class State {
        none, text, number, comparison, fen, stringlit
    };

    std::vector<LexWord> words;

    std::string text;
    auto state = State::none;
    auto ok = true;
    char quoteChar = 0; // which of "/' opened the current stringlit

    static const char* comparisonChars = "=<>!";
    for(auto p = s; ok && error == ParseError::none; ++p) {
        switch (state) {
            case State::none:
            {
                if (!*p) {
                    ok = false;
                    break;
                }

                if (isalpha(*p)) {
                    state = State::text;
                    text = *p;
                    break;
                }
                if (isdigit(*p)) {
                    state = State::number;
                    text = *p;
                    break;
                }

                if (*p == '"' || *p == '\'') {
                    // Metadata term values (player/event/site/eco/result
                    // names, see MetaField) -- quoted so they can contain
                    // spaces and the "*"/"?" glob wildcards without
                    // colliding with PQL's own operator characters.
                    state = State::stringlit;
                    quoteChar = *p;
                    text.clear();
                    break;
                }

                if (strchr(comparisonChars, *p)) {
                    state = State::comparison;
                    text = *p;
                    break;
                }

                LexWord word;
                switch (*p) {
                    case '+':
                        word.lex = Lex::operator_add;
                        break;
                    case '-':
                        word.lex = Lex::operator_sub;
                        break;
                    case '*':
                        word.lex = Lex::operator_multi;
                        break;
                    case '/':
                        word.lex = Lex::operator_div;
                        break;
                    case '#':
                        word.lex = Lex::operator_pattern_shift;
                        break;
                    case '{':
                    case '}':
                    case '(':
                    case ')':
                    case '[':
                    case ']':
                        word.lex = Lex::bracket;
                        break;
                    case ',':
                        word.lex = Lex::comma;
                        break;

                    default:
                        if (!*p) {
                            ok = false;
                            break;
                        }
                        break;
                }

                if (word.lex != Lex::none) {
                    word.string += *p;
                    words.push_back(word);
                    break;
                }
                break;
            }

            case State::text:
                if (!isalnum(*p)) {
                    if (*p == '/') { //  || *p == '-'
                        state = State::fen;
                        --p;
                        break;
                    }

                    state = State::none;
                    --p;

                    LexWord word;
                    word.lex = Lex::string;
                    word.string = text;

                    if (text == "and") {
                        word.lex = Lex::operator_and;
                    } else if (text == "or") {
                        word.lex = Lex::operator_or;
                    }
                    words.push_back(word);
                    break;
                }
                text += *p;
                break;

            case State::fen:
                if (!isalnum(*p) && *p != '/' && *p != '-' && *p != ' ') {
                    state = State::none;
                    --p;

                    LexWord word;
                    word.lex = Lex::fen;
                    word.string = text;

                    words.push_back(word);
                    break;
                }
                text += *p;
                break;

            case State::comparison:
            {
                if (strchr(comparisonChars, *p)) {
                    text += *p;
                    break;
                }

                state = State::none;
                --p;

                if (string2operator(text) >= Operator::none) {
                    // wrong
                    error = ParseError::wrong_lexical;
                    break;
                }
                LexWord word;
                word.lex = Lex::operator_comparison;
                word.string = text;
                words.push_back(word);
                break;
            }

            case State::number:
                if (!isdigit(*p)) {
                    if (isalnum(*p) || *p == '/' || *p == '-') {
                        state = State::fen;
                        --p;
                        break;
                    }

                    state = State::none;

                    if (isalpha(*p)) {
                        // wrong
                        error = ParseError::wrong_lexical;
                        break;
                    }
                    --p;

                    LexWord word;
                    word.lex = Lex::number;
                    word.string = text;
                    words.push_back(word);
                    break;
                }
                text += *p;
                break;

            case State::stringlit:
                if (*p == quoteChar) {
                    state = State::none;

                    LexWord word;
                    word.lex = Lex::stringlit;
                    word.string = text;
                    words.push_back(word);
                    break;
                }
                if (!*p) {
                    // unterminated string literal
                    error = ParseError::wrong_lexical;
                    break;
                }
                text += *p;
                break;

            default:
                break;
        }
    }

    return words;
}
