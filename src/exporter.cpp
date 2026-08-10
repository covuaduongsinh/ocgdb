/**
 * This file is part of Open Chess Game Database Standard.
 *
 * Copyright (c) 2021-2022 Nguyen Pham (github@nguyenpham)
 * Copyright (c) 2021-2022 Developers
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include "exporter.h"

using namespace ocgdb;

bool Exporter::openDB(const std::string& dbPath)
{
    if (!DbRead::openDB(dbPath)) {
        return false;
    }
    
    flag = bslib::BoardCore::ParseMoveListFlag_create_san;

    if (searchField == SearchField::moves1) {
        flag |= bslib::BoardCore::ParseMoveListFlag_move_size_1_byte;
    }
    
    // NOT wired up here via threadMap: getThreadRecord() (core.cpp)
    // creates each thread's ThreadRecord lazily, on that thread's first
    // call, which is always later than openDB() (run once, up front, on
    // the calling thread only) -- threadMap is empty at this point, so a
    // loop over it here would silently set up statements on ThreadRecord
    // instances no worker thread ever uses. See processAGame() below,
    // which lazily creates them per-thread instead, the same pattern
    // insertEvalStatement/insertVariationStatement already use on the
    // import side (builder.cpp/addgame.cpp).
    if (searchField != SearchField::moves) {
        hasGameTreeTable = DbRead::hasTable(mDb, "GameTree");
    }
    return true;
}

void Exporter::runTask()
{
    startTime = getNow();

    auto pgnPath = paraRecord.pgnPaths.front();
    pgnOfs = bslib::Funcs::openOfstream2write(pgnPath);

    paraRecord.optionFlag |= query_flag_print_pgn;
    
    for(auto && dbPath : paraRecord.dbPaths) {
        std::cout   << "Convert a database into a PGN file...\n"
                    << "DB path : " << dbPath
                    << "\nPGN path: " << pgnPath
                    << std::endl;

        readADb(dbPath, DbRead::fullGameQueryString);
    }
    
    pgnOfs.close();
}

void Exporter::processAGame(const bslib::PgnRecord& record, const std::vector<int8_t>& moveVec)
{
    assert(!record.moveString.empty() || record.moveText || !moveVec.empty());
    assert(record.gameID > 0);

    auto t = getThreadRecord(); assert(t);

    if (!t->board) {
        t->board = bslib::Funcs::createBoard(bslib::ChessVariant::standard);
    }
    assert(t->board);
    
    t->board->newGame(record.fenText);
    t->board->fromMoveList(&record, moveVec, flag, nullptr);

    if (searchField != SearchField::moves) {
        if (!t->queryComments) {
            t->queryComments = new SQLite::Statement(*mDb, "SELECT * FROM Comments WHERE GameID = ?");
        }
        if (hasGameTreeTable && !t->queryVariations) {
            t->queryVariations = new SQLite::Statement(*mDb, "SELECT Ply, Variation FROM GameTree WHERE GameID = ?");
        }
    }

    if (t->queryComments) {
        t->queryComments->reset();
        t->queryComments->bind(1, record.gameID);

        // See the identical check in DbRead::queryForABoard() (dbread.cpp)
        // for why this can't just call getColumn("Nag") unconditionally.
        auto hasNagColumn = false;
        for (int i = 0; i < t->queryComments->getColumnCount(); i++) {
            if (std::string(t->queryComments->getColumnName(i)) == "Nag") { hasNagColumn = true; break; }
        }

        while (t->queryComments->executeStep()) {
            auto comment = t->queryComments->getColumn("Comment").getString();
            auto ply = t->queryComments->getColumn("Ply").getInt();
            auto nag = hasNagColumn ? t->queryComments->getColumn("Nag").getInt() : 0;

            if (ply >= 0 && ply < t->board->getHistListSize()) {
                if (!comment.empty()) t->board->_getHistPointerAt(ply)->comment = comment;
                if (nag != 0) t->board->_getHistPointerAt(ply)->nag = nag;
            } else if (!comment.empty()) {
                t->board->setFirstComment(comment);
            }
        }
    }

    if (t->queryVariations) {
        t->queryVariations->reset();
        t->queryVariations->bind(1, record.gameID);
        while (t->queryVariations->executeStep()) {
            auto ply = t->queryVariations->getColumn("Ply").getInt();
            auto variation = t->queryVariations->getColumn("Variation").getString();
            if (ply >= 0 && ply < t->board->getHistListSize() && !variation.empty()) {
                t->board->_getHistPointerAt(ply)->variationText = variation;
            }
        }
    }

    auto toPgnString = t->board->toPgn(&record);
    if (!toPgnString.empty()) {
        std::lock_guard<std::mutex> dolock(pgnOfsMutex);
        pgnOfs << toPgnString << "\n" << std::endl;
    }

}
