/**
 * This file is part of Open Chess Game Database Standard.
 *
 * Copyright (c) 2021-2022 Nguyen Pham (github@nguyenpham)
 * Copyright (c) 2021-2022 Developers
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include <iostream>

#include "records.h"
#include "report.h"
#include "search.h"
#include "exporter.h"
#include "duplicate.h"
#include "builder.h"
#include "extract.h"
#include "addgame.h"
#include "server.h"
#include "indexer.h"
#include "optimizer.h"

#include "board/chess.h"

void print_usage();
extern bool debugMode;
extern bool progressMode;

void runTask(ocgdb::ParaRecord& param)
{
    ocgdb::Core* core = nullptr;

    switch (param.task) {
        case ocgdb::Task::create:
        {
            core = new ocgdb::Builder;
            break;
        }
        case ocgdb::Task::export_:
        {
            core = new ocgdb::Exporter;
            break;
        }
        case ocgdb::Task::dup:
        {
            core = new ocgdb::Duplicate;
            break;
        }
        case ocgdb::Task::bench:
        {
            auto search = new ocgdb::Search;
            search->setupForBench(param);
            core = search;
            break;
        }
        case ocgdb::Task::query:
        {
            core = new ocgdb::Search;
            break;
        }
        case ocgdb::Task::getgame:
        {
            core = new ocgdb::Extract;
            break;
        }
        case ocgdb::Task::merge:
        {
            core = new ocgdb::AddGame;
            break;
        }
        case ocgdb::Task::server:
        {
            core = new ocgdb::WebServer;
            break;
        }
        case ocgdb::Task::index:
        {
            core = new ocgdb::Indexer;
            break;
        }
        case ocgdb::Task::optimize:
        {
            core = new ocgdb::Optimizer;
            break;
        }

        default:
            break;
    }
    
    if (core) {
        core->run(param);
        delete core;
    }
}

void printConflictedTasks(ocgdb::Task task0, ocgdb::Task task1)
{
    std::cerr << "Error: multi/conflicted tasks: " << ocgdb::ParaRecord::toString(task0) << " vs "  << ocgdb::ParaRecord::toString(task1) << std::endl;
}

int main(int argc, const char * argv[]) {
    std::cout << "Open Chess Game Database Standard (OCGDB), (C) 2022 - version: " << ocgdb::VersionString << "\n" << std::endl;
    
    if (argc < 2) {
        print_usage();
        return 0;
    }

    // init
    {
        bslib::ChessBoard::staticInit();
    }

    auto errCnt = 0;
    ocgdb::ParaRecord paraRecord;
    
    for(auto i = 1; i < argc; i++) {
        auto oldTask = paraRecord.task;
        auto str = std::string(argv[i]);
        if (str == "-bench") {
            paraRecord.task = ocgdb::Task::bench;
            continue;
        }
        if (str == "-debug") {
            debugMode = true;
            continue;
        }
        if (str == "-progress") {
            progressMode = true;
            continue;
        }
        if (str == "-create" || str == "-merge" || str == "-export" || str == "-dup" || str == "-server"
            || str == "-index" || str == "-optimize") {
            if (str == "-create") {
                paraRecord.task = ocgdb::Task::create;
            } else if (str == "-merge") {
                paraRecord.task = ocgdb::Task::merge;
            } else if (str == "-export") {
                paraRecord.task = ocgdb::Task::export_;
            } else if (str == "-dup") {
                paraRecord.task = ocgdb::Task::dup;
            } else if (str == "-server") {
                paraRecord.task = ocgdb::Task::server;
            } else if (str == "-index") {
                paraRecord.task = ocgdb::Task::index;
            } else if (str == "-optimize") {
                paraRecord.task = ocgdb::Task::optimize;
            }
            if (oldTask != ocgdb::Task::none) {
                errCnt++;
                printConflictedTasks(oldTask, paraRecord.task);
            }
            continue;
        }

        if (i + 1 >= argc) continue;

        if (str == "-pgn") {
            paraRecord.pgnPaths.push_back(std::string(argv[++i]));
            continue;
        }
        if (str == "-db") {
            paraRecord.dbPaths.push_back(std::string(argv[++i]));
            continue;
        }
        if (str == "-r") {
            paraRecord.reportPath = std::string(argv[++i]);
            continue;
        }
        if (str == "-cpu") {
            paraRecord.cpuNumber = std::atoi(argv[++i]);
            continue;
        }
        if (str == "-elo") {
            paraRecord.limitElo = std::atoi(argv[++i]);
            continue;
        }
        if (str == "-o") {
            auto optionString = std::string(argv[++i]);
            paraRecord.setupOptions(optionString);
            continue;
        }
        if (str == "-plycount") {
            paraRecord.limitLen = std::atoi(argv[++i]);
            continue;
        }
        if (str == "-resultcount") {
            paraRecord.resultNumberLimit = std::atoi(argv[++i]);
            continue;
        }
        if (str == "-desc") {
            paraRecord.desc = std::string(argv[++i]);
            continue;
        }
        if (str == "-port") {
            paraRecord.port = std::atoi(argv[++i]);
            continue;
        }
        if (str == "-web") {
            paraRecord.webDir = std::string(argv[++i]);
            continue;
        }
        if (str == "-admintoken") {
            paraRecord.adminToken = std::string(argv[++i]);
            continue;
        }
        if (str == "-admindb") {
            paraRecord.adminDbPath = std::string(argv[++i]);
            continue;
        }
        if (str == "-root") {
            paraRecord.rootDir = std::string(argv[++i]);
            continue;
        }
        if (str == "-q" || str == "-g") {
            if (str == "-q") {
                paraRecord.task = ocgdb::Task::query;
                auto query = std::string(argv[++i]);
                paraRecord.queries.push_back(query);
            } else {
                paraRecord.task = ocgdb::Task::getgame;
                paraRecord.gameIDVec.push_back(std::atoi(argv[++i]));
            }
            // -q and -g are meant to be repeated (see usage text above: "repeat
            // to add multi queries/IDs"), so repeating the *same* task here is
            // not a conflict -- only flag it when a different task was already
            // selected (e.g. "-create ... -q ...").
            if (oldTask != ocgdb::Task::none && oldTask != paraRecord.task) {
                errCnt++;
                printConflictedTasks(oldTask, paraRecord.task);
                break;
            }
            continue;
        }
        errCnt++;
        std::cerr << "Error: unknown parameter: " << str << "\n" << std::endl;
        break;
    }
    
    if (errCnt == 0) {
        if (debugMode) {
            std::cout << "All parameters:\n" << paraRecord.toString() << std::endl;
        }
        
        if (paraRecord.isValid()) {
            runTask(paraRecord);
            return 0;
        }
        
        auto errorString = paraRecord.getErrorString();
        if (!errorString.empty()) {
            std::cerr << "Error: " << errorString << "\n" << std::endl;
        }
    }

    print_usage();
    return 1;
}

void print_usage()
{
    
    const std::string str =
    "Usage:\n" \
    " ocgdb [<parameters>]\n" \
    "\n" \
    " -create               create a new database from multi PGN files, works with -db, -pgn\n" \
    " -merge                merge multi PGN files or databases into the first database, works with -db, -pgn\n" \
    " -dup                  check duplicate games in databases, works with -db\n" \
    " -export               export from a database into a PGN file, works with -db, -pgn\n" \
    " -bench                benchmarch querying games speed, works with -db\n" \
    " -q <query>            querying positions, repeat to add multi queries, works with -db, -pgn\n" \
    " -g <id>               get game with game ID numbers (repeat to add multi IDs), works with -db, -pgn\n" \
    " -server               start a local web UI/API server for browsing a database, works with -db\n" \
    " -index                build secondary SQL indexes on Games/Comments to speed up filtering/sorting, works with -db\n" \
    " -optimize             run SQLite maintenance (ANALYZE always; VACUUM/integrity_check via -o), works with -db\n" \
    " -pgn <file>           PGN game database file, repeat to add multi files\n" \
    " -db <file>            database file, extension should be .ocgdb.db3, repeat to add multi files\n" \
    " -r <file>             report file, works with -g, -q, -dup\n" \
    "                       use :memory: to create in-memory database\n" \
    " -elo <n>              discard games with Elo under n (for creating)\n" \
    " -plycount <n>         discard games with ply-count under n (for creating)\n" \
    " -resultcount <n>      stop querying if the number of results above n (for querying)\n" \
    " -cpu <n>              number of threads, should <= total physical cores, omit it for using all cores\n" \
    " -desc \"<string>\"      a description to write to the table Info when creating a new database\n" \
    " -port <n>             HTTP port for -server, default 3456, binds 127.0.0.1 only\n" \
    " -web <dir>            folder with the web UI files for -server, default ./web\n" \
    " -admintoken <t>       token required on X-OCGDB-Token for /api/admin/*, default: random, printed on start\n" \
    " -admindb <file>       where -server keeps its own state (registered databases, job history), default: next to the exe\n" \
    " -root <dir>           when set, -server rejects any database/PGN/report path outside this folder\n" \
    " -progress             print a machine-readable \"@@PROGRESS ...\" line while running (used by -server's job runner)\n" \
    " -o [<options>,]       options, separated by commas\n" \
    "    moves              create text move field Moves\n" \
    "    moves1             create binary move field Moves, 1-byte encoding\n" \
    "    moves2             create binary move field Moves, 2-byte encoding\n" \
    "    acceptnewtags      create a new field for a new PGN tag (for creating)\n" \
    "    discardcomments    discard all comments (for creating)\n" \
    "    discardsites       discard all Site tag (for creating)\n" \
    "    discardnoelo       discard games without player Elos (for creating)\n" \
    "    discardfen         discard games with FENs (not started from origin; for creating)\n" \
    "    reseteco           re-create all ECO (for creating)\n" \
    "    printall           print all results (for querying, checking duplications)\n" \
    "    printfen           print FENs of results (for querying)\n" \
    "    printpgn           print simple PGNs of results (for querying)\n" \
    "    embededgames       duplicate included games inside other games\n" \
    "    remove             remove duplicate games (for checking duplicates)\n" \
    "    nobot              Lichess: ignore BOT games (for creating a database)\n" \
    "    bot                Lichess: count games with BOT (for creating a database)\n" \
    "    index              also build secondary SQL indexes right after creating (for creating)\n" \
    "    vacuum             also run VACUUM (for -optimize; rewrites the whole file)\n" \
    "    integrity          also run PRAGMA integrity_check (for -optimize)\n" \
    "\n" \
    "Examples:\n" \
    " ocgdb -create -pgn big.pgn -db big.ocgdb.db3 -cpu 4 -o moves\n" \
    " ocgdb -create -pgn big1.pgn -pgn big2.pgn -db :memory: -elo 2100 -o moves,moves1,discardsites\n" \
    " ocgdb -bench -db big.ocgdb.db3 -cpu 4\n" \
    " ocgdb -db big.ocgdb.db3 -cpu 4 -q \"Q=3\" -q\"P[d4, e5, f4, g4] = 4 and kb7\"\n" \
    " ocgdb -db big.ocgdb.db3 -cpu 4 -q \"fen[K7/N7/k7/8/3p4/8/N7/8 w - - 0 1]\"\n" \
    " ocgdb -db big.ocgdb.db3 -g 423 -g 4432\n" \
    " ocgdb -db big.ocgdb.db3 -dup -o remove,printall\n" \
    " ocgdb -db big.ocgdb.db3 -dup -o remove -r report.txt\n" \
    " ocgdb -server -db big.ocgdb.db3 -port 3456\n" \
    " ocgdb -server -db big.ocgdb.db3 -port 3456 -root D:\\chess -admintoken mysecret\n" \
    " ocgdb -index -db big.ocgdb.db3\n" \
    " ocgdb -optimize -db big.ocgdb.db3 -o vacuum,integrity\n"
    "\n" \
    "Main functions/features:\n" \
    "1. create an SQLite database from multi PGN files\n" \
    "2. merge/add games from multi PGN files/databases into an SQLite database\n" \
    "3. export multi SQLite databases to a PGN file\n" \
    "4. get/display PGN games/FEN strings with game IDs from an SQLite database\n" \
    "5. find duplicates/embedded games from multi SQLite databases\n" \
    "6. query games from multi SQLite databases or PGN files, using PQL (Position Query Language)\n" \
    "7. serve a local web UI/API to browse a database, view games and run PQL queries\n" \
    "8. build secondary SQL indexes on a database to speed up filtering/sorting\n" \
    "9. run routine SQLite maintenance (ANALYZE/VACUUM/integrity_check) on a database\n"
    ;

    std::cerr << str << std::endl;
}
