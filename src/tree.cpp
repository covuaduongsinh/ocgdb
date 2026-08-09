/**
 * This file is part of Open Chess Game Database Standard.
 *
 * See tree.h.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include <iostream>

#include "tree.h"

using namespace ocgdb;

bool OpeningTreeBuilder::openDB(const std::string& dbPath)
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
        mDb->exec("DROP TABLE IF EXISTS OpeningTree");
        // WITHOUT ROWID: the natural key IS (HashKey, San), and this table
        // is looked up almost exclusively by HashKey (GET /api/tree,
        // server.cpp) -- a composite primary key already gives that
        // lookup a btree ordered on HashKey first, so no secondary index
        // is needed.
        mDb->exec(
            "CREATE TABLE OpeningTree ("
            "HashKey INTEGER NOT NULL, San TEXT NOT NULL,"
            "Games INTEGER, WhiteWin INTEGER, Draw INTEGER, BlackWin INTEGER,"
            "EloSum INTEGER, EloCnt INTEGER, LastYear INTEGER,"
            "PRIMARY KEY (HashKey, San)) WITHOUT ROWID");
    } catch (std::exception& e) {
        std::cerr << "Error: could not create OpeningTree: " << e.what() << std::endl;
        return false;
    }

    mDb->exec("PRAGMA journal_mode=OFF");
    return true;
}

void OpeningTreeBuilder::closeDb()
{
    // mDb is still open/valid here -- readADb() (dbread.cpp) calls this
    // right after the worker pool has finished every game but before it
    // deletes mDb, which is exactly when the merged aggregation needs to
    // be written out.
    if (mDb) {
        mergeAndSave();
    }
    DbRead::closeDb();
}

void OpeningTreeBuilder::runTask()
{
    std::cout << "Building opening tree (depth " << paraRecord.treeDepth << " plies)..." << std::endl;

    startTime = getNow();
    // Need Result/Date/WhiteElo/BlackElo, which DbRead::extractHeader()
    // (dbread.cpp) only populates into record.tags when this is set --
    // see readADb()'s per-row loop.
    paraRecord.optionFlag |= query_flag_print_pgn;

    for (auto&& dbPath : paraRecord.dbPaths) {
        std::cout << "DB path: " << dbPath << std::endl;
        gameCnt = commentCnt = 0;
        eventCnt = playerCnt = siteCnt = 1;
        errCnt = 0;
        edgeCnt = 0;
        {
            std::lock_guard<std::mutex> lk(perThreadMutex);
            perThreadAgg.clear();
        }
        readADb(dbPath, DbRead::fullGameQueryString);
    }
}

void OpeningTreeBuilder::printStats() const
{
    // NOT the place to print edgeCnt: readADb() (dbread.cpp) calls this
    // right after the worker pool finishes every game but *before*
    // closeDb() (where mergeAndSave() actually writes OpeningTree and
    // increments edgeCnt) -- printing it here would always show 0.
    // mergeAndSave() reports its own edge count once it actually has one.
    DbCore::printStats();
    std::cout << std::endl;
}

OpeningTreeBuilder::EdgeMap& OpeningTreeBuilder::mapForThread(std::thread::id id)
{
    std::lock_guard<std::mutex> lk(perThreadMutex);
    return perThreadAgg[id];
}

void OpeningTreeBuilder::mergeAndSave()
{
    EdgeMap merged;
    {
        std::lock_guard<std::mutex> lk(perThreadMutex);
        for (auto&& threadEntry : perThreadAgg) {
            for (auto&& hashEntry : threadEntry.second) {
                auto& dstBySan = merged[hashEntry.first];
                for (auto&& sanEntry : hashEntry.second) {
                    auto& dst = dstBySan[sanEntry.first];
                    auto& src = sanEntry.second;
                    dst.games += src.games;
                    dst.whiteWin += src.whiteWin;
                    dst.draw += src.draw;
                    dst.blackWin += src.blackWin;
                    dst.eloSum += src.eloSum;
                    dst.eloCnt += src.eloCnt;
                    if (src.lastYear > dst.lastYear) dst.lastYear = src.lastYear;
                }
            }
        }
        perThreadAgg.clear();
    }

    if (merged.empty()) {
        return;
    }

    try {
        sendTransaction(true);
        SQLite::Statement stmt(*mDb,
            "INSERT INTO OpeningTree (HashKey, San, Games, WhiteWin, Draw, BlackWin, EloSum, EloCnt, LastYear) "
            "VALUES (?,?,?,?,?,?,?,?,?)");
        for (auto&& hashEntry : merged) {
            for (auto&& sanEntry : hashEntry.second) {
                auto& a = sanEntry.second;
                stmt.reset();
                stmt.bind(1, hashEntry.first);
                stmt.bind(2, sanEntry.first);
                stmt.bind(3, a.games);
                stmt.bind(4, a.whiteWin);
                stmt.bind(5, a.draw);
                stmt.bind(6, a.blackWin);
                stmt.bind(7, a.eloSum);
                stmt.bind(8, a.eloCnt);
                stmt.bind(9, a.lastYear);
                stmt.exec();
                edgeCnt++;
            }
        }
        sendTransaction(false);
        std::cout << "  saved " << edgeCnt << " opening-tree edges" << std::endl;
    } catch (std::exception& e) {
        std::cerr << "Error: could not save OpeningTree: " << e.what() << std::endl;
    }
}

void OpeningTreeBuilder::processAGameWithAThread(ThreadRecord* t, const bslib::PgnRecord& record, const std::vector<int8_t>& moveVec)
{
    assert(t);

    if (!t->board) {
        t->board = bslib::Funcs::createBoard(bslib::ChessVariant::standard);
    }
    assert(t->board);

    t->board->newGame(record.fenText);

    int flag = bslib::BoardCore::ParseMoveListFlag_create_san;
    if (searchField == SearchField::moves) { // there is a text move only
        flag |= bslib::BoardCore::ParseMoveListFlag_quick_check;
        t->board->fromMoveList(&record, bslib::Notation::san, flag, nullptr);
    } else {
        if (searchField == SearchField::moves1) {
            flag |= bslib::BoardCore::ParseMoveListFlag_move_size_1_byte;
        }
        t->board->fromMoveList(&record, moveVec, flag, nullptr);
    }

    std::string result;
    {
        auto it = record.tags.find("Result");
        if (it != record.tags.end()) result = it->second;
    }
    int64_t whiteWinInc = 0, drawInc = 0, blackWinInc = 0;
    if (result == "1-0") whiteWinInc = 1;
    else if (result == "0-1") blackWinInc = 1;
    else if (result == "1/2-1/2") drawInc = 1;
    // Any other value (unfinished game, "*", missing tag): still counts
    // toward Games below, just not toward any of the three W/D/L buckets.

    int64_t eloSum = 0, eloCnt = 0;
    {
        auto it = record.tags.find("WhiteElo");
        if (it != record.tags.end() && !it->second.empty()) {
            eloSum += std::atoi(it->second.c_str());
            eloCnt++;
        }
    }
    {
        auto it = record.tags.find("BlackElo");
        if (it != record.tags.end() && !it->second.empty()) {
            eloSum += std::atoi(it->second.c_str());
            eloCnt++;
        }
    }

    int64_t year = 0;
    {
        auto it = record.tags.find("Date");
        // Builder::standardizeDate() (builder.cpp) normalizes Date to
        // "YYYY.MM.DD" or "YYYY-MM-DD"; either way the first 4 characters
        // are the year.
        if (it != record.tags.end() && it->second.size() >= 4) {
            year = std::atoi(it->second.substr(0, 4).c_str());
        }
    }

    auto& edgeMap = mapForThread(std::this_thread::get_id());

    auto n = t->board->getHistListSize();
    auto limit = std::min(n, paraRecord.treeDepth);
    for (int i = 0; i < limit; i++) {
        auto hist = t->board->_getHistPointerAt(i);
        // Hist::hashKey is the position's hash *before* this move was
        // made (see ChessBoard::_make(), chess.cpp -- it is assigned from
        // the board's live hashKey right before any of that move's XOR
        // updates happen), i.e. exactly the parent position this move was
        // played from. sanString needs ParseMoveListFlag_create_san,
        // passed above.
        if (!hist || hist->sanString.empty()) continue;

        auto& agg = edgeMap[hist->hashKey][hist->sanString];
        agg.games++;
        agg.whiteWin += whiteWinInc;
        agg.draw += drawInc;
        agg.blackWin += blackWinInc;
        agg.eloSum += eloSum;
        agg.eloCnt += eloCnt;
        if (year > agg.lastYear) agg.lastYear = year;
    }

    t->gameCnt++;
}
