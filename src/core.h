/**
 * This file is part of Open Chess Game Database Standard.
 *
 * Copyright (c) 2021-2022 Nguyen Pham (github@nguyenpham)
 * Copyright (c) 2021-2022 Developers
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#ifndef OCGDB_CORE_H
#define OCGDB_CORE_H

#include <stdio.h>
#include <atomic>
#include <unordered_map>

#include "3rdparty/threadpool/thread_pool.hpp"

#include "records.h"
#include "report.h"

namespace ocgdb {


class Core
{
public:
    Core();
    virtual ~Core();

    virtual void run(const ocgdb::ParaRecord&);
    ThreadRecord* getThreadRecord();
    virtual void resetCnts();

protected:
    virtual void runTask() = 0;

    void createPool();
    virtual void printStats() const;
    static std::chrono::steady_clock::time_point getNow();

protected:
    bslib::ChessVariant chessVariant = bslib::ChessVariant::standard;

    ParaRecord paraRecord;
    
    mutable std::mutex threadMapMutex;
    mutable std::mutex printMutex;
    
    static thread_pool* pool;
    std::unordered_map<std::thread::id, ThreadRecord> threadMap;
    
    IDInteger gameCnt, eventCnt, playerCnt, siteCnt, commentCnt, epdCnt, itemCnt;

    /// For stats
    std::chrono::steady_clock::time_point startTime;
    int64_t blockCnt, processedPgnSz, processedCnt, workingGameIdx, errCnt;
    // Incremented from worker threads (Search::processAGameWithAThread,
    // WebServer::processAGameWithAThread) while the main thread
    // concurrently reads it (readADb()'s resultNumberLimit check) --
    // a plain int64_t here was a real data race with observed lost
    // updates (identical -cpu 4 queries returning counts off by one
    // between runs). Must stay atomic.
    std::atomic<int64_t> succCount;
    // Total size (bytes) of the PGN file(s) being read, if any -- set by
    // PgnRead before its read loop (pgnread.cpp) so printStats() can turn
    // processedPgnSz into a real percentage for -progress. Zero/unused for
    // tasks that don't read PGN files (bench, query against a .db3, etc).
    int64_t pgnTotalSz;
};

} // namespace ocdb

#endif /* OCGDB_CORE_H */
