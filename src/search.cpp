/**
 * This file is part of Open Chess Game Database Standard.
 *
 * Copyright (c) 2021-2022 Nguyen Pham (github@nguyenpham)
 * Copyright (c) 2021-2022 Developers
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include "search.h"

using namespace ocgdb;

Search::~Search()
{
    if (qgr) {
        delete qgr;
        qgr = nullptr;
    }
}

void Search::runTask()
{
    std::cout   << "Querying..." << std::endl;

    if (paraRecord.dbPaths.empty() && paraRecord.pgnPaths.empty()) {
        std::cout << "Error: there is no path for database nor PGN files" << std::endl;
        return;
    }
    
    if (paraRecord.queries.empty()) {
        std::cout << "Error: there is no query" << std::endl;
        return;
    }
    
    assert(paraRecord.task != Task::create);

    gameCnt = commentCnt = 0;
    eventCnt = playerCnt = siteCnt = 1;
    errCnt = 0;
    succCount = 0;

    checkToStop = nullptr;
    
    boardCallback = [=](const bslib::BoardCore* board, const bslib::PgnRecord* record) -> bool {
        assert(board);

        // histList[i] holds the position right after move i+1 was made (0
        // indexed by ply), so iterating over every position in the game
        // means i = 0 .. n-1 inclusive. The last entry is refetched via
        // posToBitboards() rather than trusting histList[n-1].bitboardVec,
        // matching the original intent here.
        for(int i = 0, n = board->getHistListSize(); i < n; i++) {
            std::vector<uint64_t> bitboardVec;

            if (i < n - 1) {
                auto hist = board->_getHistPointerAt(i);
                assert(hist && !hist->bitboardVec.empty());
                bitboardVec = hist->bitboardVec;
            } else {
                // last position
                bitboardVec = board->posToBitboards();
            }

            if (!parser.evaluate(bitboardVec)) {
                continue;
            }

            succCount++;

            if (paraRecord.optionFlag & query_flag_print_all) {
                std::lock_guard<std::mutex> dolock(printMutex);

                std::cout << succCount << ". gameId: " << (record ? record->gameID : -1) << std::endl;
            }

            if (printOut.isOn()) {
                if (paraRecord.optionFlag & query_flag_print_fen) {
                    std::string str = std::to_string(succCount) + ". gameId: " + std::to_string(record ? record->gameID : -1) +
                                ", fen: " + board->getFen() + "\n";
                    printOut.printOut(str);
                }

                static std::string printOutQuery;

                if (query != printOutQuery) {
                    printOutQuery = query;
                    printOut.printOut("; >>>>>> Query: " + query + "\n");
                }
                if (qgr) {
                    printGamePGNByIDs(*qgr, std::vector<int>{record->gameID});
                } else {
                    printOut.printOutPgn(*record);
                }
            }

            return true;
        }

        return false;
    };

    
    for(auto && _query : paraRecord.queries) {
        query = Parser::stripLineComments(_query);
        // Reset per query (not just once before this loop) -- otherwise
        // #succ reported after query N includes every match from queries
        // 1..N-1 too, which is exactly what makes -bench's canned queries
        // (setupForBench() below) print identical, accumulated #succ
        // numbers for unrelated queries.
        succCount = 0;

        bslib::Funcs::trim(query);

        if (query.empty()) {
            continue;;
        }

        std::cout << "Search with query " << query <<  "..." << std::endl;
        
        assert(paraRecord.task != Task::create);
        if (!parser.parse(chessVariant, query.c_str())) {
            std::cerr << "Error: " << parser.getErrorString() << std::endl;
            continue;;
        }

        // Query PGN files
        for(auto && path : paraRecord.pgnPaths) {
            startTime = getNow();
            processPgnFile(path);
        }

        // Query databases
        if (!paraRecord.dbPaths.empty()) {
            // Metadata terms (whiteplayer/welo/date/eco/...) reference
            // White/Black/Event/Site by name, and Result/WhiteElo/
            // BlackElo/Date/ECO -- all only resolved through the joined
            // fullGameQueryString, never the bare "SELECT * FROM Games".
            auto baseQueryString = (paraRecord.optionFlag & query_flag_print_pgn) || parser.metadataNeedsNames()
                                    ? DbRead::fullGameQueryString : "SELECT * FROM Games";
            // Exact (metadata) and safe-conservative (material, see
            // buildMaterialPreFilterSql()) pre-filters -- both empty when
            // nothing applies, in which case every database is scanned in
            // full exactly as before either existed.
            auto metadataWhere = parser.getMetadataWhereSql();
            auto materialFilter = parser.buildMaterialPreFilterSql();
            for(auto && dbPath : paraRecord.dbPaths) {
                gameCnt = commentCnt = 0;
                eventCnt = playerCnt = siteCnt = 1;
                errCnt = 0;
                succCount = 0;

                std::vector<std::string> whereClauses;
                if (!metadataWhere.empty()) whereClauses.push_back(metadataWhere);
                if (!materialFilter.empty()) {
                    try {
                        SQLite::Database probe(dbPath, SQLite::OPEN_READONLY);
                        if (DbRead::hasTable(&probe, "GameMaterial")) {
                            whereClauses.push_back("ID IN (SELECT GameID FROM GameMaterial WHERE " + materialFilter + ")");
                        }
                    } catch (std::exception&) {
                        // fall back to unfiltered for this specific clause
                    }
                }

                auto queryString = baseQueryString;
                if (!whereClauses.empty()) {
                    std::string combined;
                    for (auto&& w : whereClauses) {
                        if (!combined.empty()) combined += " AND ";
                        combined += "(" + w + ")";
                    }
                    queryString = "SELECT * FROM (" + baseQueryString + ") WHERE " + combined;
                }
                readADb(dbPath, queryString);
            }
        }
    }
}


void Search::processAGameWithAThread(ThreadRecord* t, const bslib::PgnRecord& record, const std::vector<int8_t>& moveVec)
{
    assert(t);

    if (!t->board) {
        t->board = bslib::Funcs::createBoard(bslib::ChessVariant::standard);
    }
    assert(t->board);
    
    t->board->newGame(record.fenText);
    
    int flag = bslib::BoardCore::ParseMoveListFlag_create_bitboard;
    if (searchField == SearchField::moves) { // there is a text move only
        flag |= bslib::BoardCore::ParseMoveListFlag_quick_check;
        t->board->fromMoveList(&record, bslib::Notation::san, flag, checkToStop);
    } else {
        if (searchField == SearchField::moves1) {
            flag |= bslib::BoardCore::ParseMoveListFlag_move_size_1_byte;
        }
        
        if (paraRecord.optionFlag & query_flag_print_pgn) {
            flag |= bslib::BoardCore::ParseMoveListFlag_create_san;
        }
        t->board->fromMoveList(&record, moveVec, flag, checkToStop);
    }

    t->hdpLen += t->board->getHistListSize();

    if (boardCallback) {
        boardCallback(t->board, &record);
    }
    t->gameCnt++;
}

void Search::printStats() const
{
    DbCore::printStats();
    std::cout << " #succ: " << succCount << std::endl;

}

void Search::processPGNGameWithAThread(ThreadRecord* t, const std::unordered_map<char*, char*>& tagMap, const char* moveText)
{
    assert(t);

    if (!t->board) {
        t->board = bslib::Funcs::createBoard(bslib::ChessVariant::standard);
    }
    assert(t->board);
    
    bslib::PgnRecord record;
    {
        std::lock_guard<std::mutex> dolock(gameIDMutex);
        ++gameCnt;
        record.gameID = gameCnt;
    }

    record.moveText = moveText;
    
    for(auto && it : tagMap) {
        auto name = std::string(it.first), s = std::string(it.second);
        if (name == "FEN") {
            record.fenText = s;
        }
        record.tags[name] = s;
    }

    // Parse moves
    t->board->newGame(record.fenText);

    int flag = bslib::BoardCore::ParseMoveListFlag_quick_check
                | bslib::BoardCore::ParseMoveListFlag_discardComment
                | bslib::BoardCore::ParseMoveListFlag_create_bitboard;

    if (paraRecord.optionFlag & query_flag_print_pgn) {
        flag |= bslib::BoardCore::ParseMoveListFlag_create_san;
    }
    t->board->fromMoveList(&record, bslib::Notation::san, flag, checkToStop);
    
    if (boardCallback) {
        boardCallback(t->board, &record);
    }

}

bool Search::openDB(const std::string& dbPath)
{
    if (DbRead::openDB(dbPath)) {
        if (qgr) {
            delete qgr;
        }

        startTime = getNow();
        
        qgr = new QueryGameRecord(*mDb, searchField);
        return true;
    }
    return false;
}

void Search::closeDb()
{
    if (qgr) {
        delete qgr;
        qgr = nullptr;
    }
    DbRead::closeDb();
}


void Search::setupForBench(ParaRecord& paraRecord)
{
    std::cout << "Benchmark position searching..." << std::endl;

    paraRecord.queries = std::vector<std::string> {
        "Q = 3",                            // three White Queens
        "r[e4, e5, d4,d5]= 2",              // two black Rooks in middle squares
        "P[d4, e5, f4, g4] = 4 and kb7",    // White Pawns in d4, e5, f4, g4 and black King in b7
        "B[c-f] + b[c-f] == 2",               // There are two Bishops (any side) from column c to f
        "white6 = 5",                        // There are 5 white pieces on row 6
    };

//    searchPostion(paraRecord);
}
