/**
 * This file is part of Open Chess Game Database Standard.
 *
 * See indexer.h.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include <chrono>
#include <iostream>
#include <set>

#include "indexer.h"

using namespace ocgdb;

void ocgdb::buildStandardIndexes(SQLite::Database& db, int& createdCnt, int& skippedCnt)
{
    createdCnt = skippedCnt = 0;

    // Games' schema is generated per-database from whichever PGN tags were
    // actually present in the source (see Builder::createDb, builder.cpp),
    // so a column this list wants to index may simply not exist here --
    // skip it rather than let CREATE INDEX throw.
    std::set<std::string> gamesCols;
    try {
        SQLite::Statement stmt(db, "PRAGMA table_info(Games)");
        while (stmt.executeStep()) {
            gamesCols.insert(stmt.getColumn(1).getText());
        }
    } catch (std::exception& e) {
        std::cerr << "Error: could not read Games schema: " << e.what() << std::endl;
        return;
    }
    if (gamesCols.empty()) {
        std::cerr << "Error: database has no Games table" << std::endl;
        return;
    }

    auto tryIndex = [&](const char* indexName, const char* table, const char* column) {
        if (std::string(table) == "Games" && gamesCols.find(column) == gamesCols.end()) {
            return;
        }
        try {
            bool existed = false;
            {
                SQLite::Statement q(db, "SELECT 1 FROM sqlite_master WHERE type='index' AND name=?");
                q.bind(1, indexName);
                existed = q.executeStep();
            }
            db.exec(std::string("CREATE INDEX IF NOT EXISTS ") + indexName + " ON " + table + "(" + column + ")");
            if (existed) skippedCnt++; else createdCnt++;
        } catch (std::exception& e) {
            std::cerr << "Warning: could not create index " << indexName << ": " << e.what() << std::endl;
        }
    };

    // Player/event/site lookups, and the ORDER BY columns /api/games
    // supports (see server.cpp's GamesParamNames + apiGamesJson).
    tryIndex("idx_games_white",    "Games", "WhiteID");
    tryIndex("idx_games_black",    "Games", "BlackID");
    tryIndex("idx_games_event",    "Games", "EventID");
    tryIndex("idx_games_site",     "Games", "SiteID");
    tryIndex("idx_games_date",     "Games", "Date");
    tryIndex("idx_games_eco",      "Games", "ECO");
    tryIndex("idx_games_result",   "Games", "Result");
    tryIndex("idx_games_plycount", "Games", "PlyCount");
    tryIndex("idx_games_welo",     "Games", "WhiteElo");
    tryIndex("idx_games_belo",     "Games", "BlackElo");
    // Comment lookups (game detail view, export, duplicate-log printing)
    // join back to Games by GameID with no index today.
    tryIndex("idx_comments_game",  "Comments", "GameID");
}

void Indexer::runTask()
{
    std::cout << "Building indexes..." << std::endl;

    startTime = getNow();
    createdCnt = skippedCnt = 0;

    for (auto&& dbPath : paraRecord.dbPaths) {
        std::cout << "DB path: " << dbPath << std::endl;
        try {
            SQLite::Database db(dbPath, SQLite::OPEN_READWRITE);
            int created = 0, skipped = 0;
            buildStandardIndexes(db, created, skipped);
            createdCnt += created;
            skippedCnt += skipped;
            std::cout << "  created " << created << ", already existed " << skipped << std::endl;
        } catch (std::exception& e) {
            std::cerr << "Error: can't open database " << dbPath << ": " << e.what() << std::endl;
            errCnt++;
        }
    }

    printStats();
}

void Indexer::printStats() const
{
    int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(getNow() - startTime).count() + 1;

    std::lock_guard<std::mutex> dolock(printMutex);
    std::cout << "#indexes created: " << createdCnt << ", already existed: " << skippedCnt
               << ", elapsed: " << elapsed << "ms" << std::endl;
}
