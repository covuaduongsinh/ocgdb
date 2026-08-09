/**
 * This file is part of Open Chess Game Database Standard.
 *
 * See material.h.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include <iostream>

#include "material.h"

using namespace ocgdb;

namespace {

int popcnt(uint64_t x)
{
    int c = 0;
    while (x) {
        c++;
        x &= x - 1;
    }
    return c;
}

} // namespace

bool MaterialBuilder::openDB(const std::string& dbPath)
{
    mDb = DbCore::openDB(dbPath, false); // always needs write access
    if (!mDb) {
        return false;
    }

    searchField = DbRead::getMoveField(mDb);
    if (searchField == SearchField::none) {
        std::cerr << "Error: database " << dbPath << " has not any move field" << std::endl;
        return false;
    }

    try {
        mDb->exec("DROP TABLE IF EXISTS GameMaterial");
        // Column names must match kPieceCols in parser.cpp
        // (Parser::buildMaterialPreFilterSql()). "w"/"b" suffixes rather
        // than upper/lowercase letters (e.g. MaxQw not MaxQ/Maxq) because
        // SQL identifiers are case-insensitive -- MaxQ and Maxq would
        // silently collide into the same column.
        mDb->exec(
            "CREATE TABLE GameMaterial ("
            "GameID INTEGER PRIMARY KEY,"
            "MaxKw INTEGER, MaxQw INTEGER, MaxRw INTEGER, MaxBw INTEGER, MaxNw INTEGER, MaxPw INTEGER,"
            "MaxKb INTEGER, MaxQb INTEGER, MaxRb INTEGER, MaxBb INTEGER, MaxNb INTEGER, MaxPb INTEGER)");
        // No index on the king columns (MaxKw/MaxKb) -- every legal
        // position has exactly one king per side, so a filter on it would
        // never narrow anything down.
        mDb->exec("CREATE INDEX idx_gm_qw ON GameMaterial(MaxQw)");
        mDb->exec("CREATE INDEX idx_gm_rw ON GameMaterial(MaxRw)");
        mDb->exec("CREATE INDEX idx_gm_bw ON GameMaterial(MaxBw)");
        mDb->exec("CREATE INDEX idx_gm_nw ON GameMaterial(MaxNw)");
        mDb->exec("CREATE INDEX idx_gm_pw ON GameMaterial(MaxPw)");
        mDb->exec("CREATE INDEX idx_gm_qb ON GameMaterial(MaxQb)");
        mDb->exec("CREATE INDEX idx_gm_rb ON GameMaterial(MaxRb)");
        mDb->exec("CREATE INDEX idx_gm_bb ON GameMaterial(MaxBb)");
        mDb->exec("CREATE INDEX idx_gm_nb ON GameMaterial(MaxNb)");
        mDb->exec("CREATE INDEX idx_gm_pb ON GameMaterial(MaxPb)");
    } catch (std::exception& e) {
        std::cerr << "Error: could not create GameMaterial: " << e.what() << std::endl;
        return false;
    }

    mDb->exec("PRAGMA journal_mode=OFF");
    sendTransaction(true);
    return true;
}

void MaterialBuilder::closeDb()
{
    if (mDb) {
        sendTransaction(false);
    }
    {
        std::lock_guard<std::mutex> lk(insertStmtMapMutex);
        for (auto&& it : insertStmtMap) delete it.second;
        insertStmtMap.clear();
    }
    DbRead::closeDb();
}

void MaterialBuilder::runTask()
{
    std::cout << "Building material index (for PQL pre-filtering)..." << std::endl;

    startTime = getNow();

    for (auto&& dbPath : paraRecord.dbPaths) {
        std::cout << "DB path: " << dbPath << std::endl;
        gameCnt = commentCnt = 0;
        eventCnt = playerCnt = siteCnt = 1;
        errCnt = 0;
        readADb(dbPath, "SELECT * FROM Games");
    }
}

void MaterialBuilder::printStats() const
{
    DbCore::printStats();
    std::cout << std::endl;
}

SQLite::Statement* MaterialBuilder::insertStatementFor(std::thread::id id)
{
    std::lock_guard<std::mutex> lk(insertStmtMapMutex);
    auto it = insertStmtMap.find(id);
    if (it != insertStmtMap.end()) {
        return it->second;
    }

    auto stmt = new SQLite::Statement(*mDb,
        "INSERT INTO GameMaterial (GameID, MaxKw, MaxQw, MaxRw, MaxBw, MaxNw, MaxPw, "
        "MaxKb, MaxQb, MaxRb, MaxBb, MaxNb, MaxPb) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)");
    insertStmtMap[id] = stmt;
    return stmt;
}

void MaterialBuilder::processAGameWithAThread(ThreadRecord* t, const bslib::PgnRecord& record, const std::vector<int8_t>& moveVec)
{
    assert(t);

    if (!t->board) {
        t->board = bslib::Funcs::createBoard(bslib::ChessVariant::standard);
    }
    assert(t->board);

    int maxCnt[12] = {0};

    auto scan = [&](const std::vector<uint64_t>& bb) {
        if (bb.empty()) return;
        auto white   = bb[static_cast<int>(bslib::BBIdx::white)];
        auto black   = bb[static_cast<int>(bslib::BBIdx::black)];
        auto kings   = bb[static_cast<int>(bslib::BBIdx::kings)];
        auto queens  = bb[static_cast<int>(bslib::BBIdx::queens)];
        auto rooks   = bb[static_cast<int>(bslib::BBIdx::rooks)];
        auto bishops = bb[static_cast<int>(bslib::BBIdx::bishops)];
        auto knights = bb[static_cast<int>(bslib::BBIdx::knights)];
        auto pawns   = bb[static_cast<int>(bslib::BBIdx::pawns)];

        int cur[12] = {
            popcnt(white & kings),   popcnt(white & queens),  popcnt(white & rooks),
            popcnt(white & bishops), popcnt(white & knights), popcnt(white & pawns),
            popcnt(black & kings),   popcnt(black & queens),  popcnt(black & rooks),
            popcnt(black & bishops), popcnt(black & knights), popcnt(black & pawns),
        };
        for (int i = 0; i < 12; i++) {
            if (cur[i] > maxCnt[i]) maxCnt[i] = cur[i];
        }
    };

    t->board->newGame(record.fenText);
    // The starting position matters too -- most games start at the
    // standard setup, but "from position" games (custom FEN) can start
    // with different material than any later ply reaches (e.g. a game
    // that only loses material from a non-standard start).
    scan(t->board->posToBitboards());

    int flag = bslib::BoardCore::ParseMoveListFlag_create_bitboard;
    if (searchField == SearchField::moves) { // there is a text move only
        flag |= bslib::BoardCore::ParseMoveListFlag_quick_check;
        t->board->fromMoveList(&record, bslib::Notation::san, flag, nullptr);
    } else {
        if (searchField == SearchField::moves1) {
            flag |= bslib::BoardCore::ParseMoveListFlag_move_size_1_byte;
        }
        t->board->fromMoveList(&record, moveVec, flag, nullptr);
    }

    // Same convention as the (fixed) PQL scan loops in search.cpp/
    // server.cpp: histList[i] is the position right after move i+1, so
    // i = 0 .. n-1 covers every played position, and the last one is
    // recomputed fresh via posToBitboards() rather than trusting the
    // cached bitboardVec of the final hist entry.
    for (int i = 0, n = t->board->getHistListSize(); i < n; i++) {
        if (i < n - 1) {
            auto hist = t->board->_getHistPointerAt(i);
            if (hist && !hist->bitboardVec.empty()) scan(hist->bitboardVec);
        } else {
            scan(t->board->posToBitboards());
        }
    }

    auto stmt = insertStatementFor(std::this_thread::get_id());
    stmt->reset();
    stmt->bind(1, record.gameID);
    for (int i = 0; i < 12; i++) {
        stmt->bind(2 + i, maxCnt[i]);
    }
    stmt->exec();

    t->gameCnt++;
}
