/**
 * This file is part of Open Chess Game Database Standard.
 *
 * Copyright (c) 2021-2022 Nguyen Pham (github@nguyenpham)
 * Copyright (c) 2021-2022 Developers
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#ifndef OCGDB_PARSER_H
#define OCGDB_PARSER_H

#include <vector>
#include <unordered_map>
#include <fstream>

#include "board/types.h"
#include "board/base.h"

namespace ocgdb {

enum class ParseError
{
    none,
    noinput,
    wrong_lexical,
    missing_condition,
    missing_op_comparison,
    missing_term,
    missing_factor,
    missing_close,
    invalid,
    max
};

enum class Lex
{
    none, string, number, fen,

    operator_begin,
    operator_and = operator_begin, operator_or,
    operator_add, operator_sub,
    operator_multi, operator_div,
    operator_comparison,
    operator_pattern_shift,

    no_node,
    comma = no_node,
    bracket,
};

class LexWord
{
public:
    Lex lex = Lex::none;
    std::string string;
};


enum class Operator
{
    op_and, op_or, op_add, op_sub, op_multi, op_div,
    op_eq, op_l, op_le, op_g, op_ge, op_ne,
    none
};

enum class NodeType
{
    none, piece, number, op, fen, pattern
};

enum class PatternOperand
{
    equal, lessthan, greaterthan, shift, none
};

class Node
{
public:
    Node() {}
    Node(const LexWord& w);

    std::string toString() const;
    int evaluate(const std::vector<uint64_t>& bitboardVec) const;
    bool isValid() const;

    int selectSquare(const char*);
    int selectSquare(const std::string& from, const std::string& to);
    
    bool isInFenHashSet(uint64_t hash) const {
        return fenHashSet.find(hash) != fenHashSet.end();
    }
    
    void pattern_shift();

private:
    bool evaluate_pattern(const std::vector<uint64_t>& bbVec0, const std::vector<uint64_t>& bbVec1, PatternOperand, int tolerance) const;

    bool pattern_shift_up(std::vector<uint64_t>&);
    bool pattern_shift_down(std::vector<uint64_t>&);
    bool pattern_shift_left(std::vector<uint64_t>&);
    bool pattern_shift_right(std::vector<uint64_t>&);

public:
    NodeType nodeType = NodeType::none;
    std::string string;
    int number;
    Operator op = Operator::none;
    Node *lhs = nullptr, *rhs = nullptr;
    
    std::set<uint64_t> fenHashSet;

    bool hassquareset = false, negative = false;
    int64_t squareset = 0;

    int pieceType = -1;
    bslib::Side pieceSide = bslib::Side::none;
    std::set<int> locSet;
    std::vector<std::vector<uint64_t>> patternBitBoards;

    PatternOperand patternOperand;
    int patternTolerance = 0;
};

class Parser
{
public:
    Parser();
    virtual ~Parser();

    bool parse(bslib::ChessVariant _variant, const char*);

    // Strips "// ..." line comments out of a raw PQL query string before
    // it reaches parse()/lexParse(). Shared so every caller that accepts a
    // PQL string from outside (CLI -q, and -server's /api/query) supports
    // the same comment syntax -- previously only the CLI path stripped
    // comments (search.cpp), so the same query with a "//" comment parsed
    // fine from the command line but failed from the web UI.
    static std::string stripLineComments(const std::string& query);

    std::string getErrorString() const;
    void printError() const;

    int evaluate(const std::vector<uint64_t>& bitboardVec) const;
    void printTree() const;

    // Derives a WHERE-clause fragment over the GameMaterial table (see
    // material.h/.cpp) expressing a SAFE, CONSERVATIVE lower bound on how
    // many of each piece type/color this query's parsed tree could
    // possibly require -- e.g. "Q = 3" implies a game must have reached at
    // least 3 white queens at *some* ply to have any chance of matching.
    // Used to pre-filter a full-database scan via GameMaterial (built by
    // -material) before replaying a single move.
    //
    // Returns an empty string when nothing useful can be derived -- always
    // a safe answer (the caller then falls back to scanning every game,
    // exactly like before this existed). Must call parse() successfully
    // first; returns empty if there is no parsed tree.
    //
    // The extraction is deliberately restricted to what can be proven safe
    // without full interval arithmetic: bare "piece cmp constant"
    // comparisons (optionally square-masked, e.g. "P[d4]>=1" -- the mask
    // only makes the true count *smaller*, so the bound from the masked
    // count still safely lower-bounds the unmasked total), combined
    // through "and" (bounds combine via max -- both sides' constraints
    // must hold at once) and "or" (bounds combine via min -- only one side
    // is guaranteed, so only what both sides agree on survives). Anything
    // else -- arithmetic combinations like "Q+R", piece-vs-piece
    // comparisons, fen/pattern clauses -- contributes no bound for the
    // pieces it involves, never a wrong one.
    std::string buildMaterialPreFilterSql() const;



private:
    void deleteTree();
    void deleteTree(Node* node) const;

    std::vector<LexWord> lexParse(const char*);

    Node* parse_fenclause(size_t&);
    Node* parse_condition(size_t&);
    Node* parse_expression(size_t&);
    Node* parse_term(size_t&);
    Node* parse_factor(size_t&);
    Node* parse_piece(size_t&);
    Node* parse_piecename(size_t&);
    Node* parse_square(size_t&);
    Node* parse_fenstring(size_t&);
    Node* parse_pattern(size_t&);
    void parse_squareset(Node*, size_t&);
 
    void parse_patternterm(size_t&);
    void parse_patterncondition(size_t&);

    void printTree(const Node* node, std::string prefix = "") const;

    static std::string getErrorString(ParseError error);

private:
    bslib::ChessVariant variant = bslib::ChessVariant::standard;
    bslib::BoardCore *board = nullptr, *board2 = nullptr;

    std::vector<LexWord> lexVec;
    Node* root = nullptr;
    
    ParseError error = ParseError::none;
};

} // namespace ocdb

#endif /* OCGDB_PARSER_H */
