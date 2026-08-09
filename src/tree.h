/**
 * This file is part of Open Chess Game Database Standard.
 *
 * Implements the `-tree` task: builds an opening tree, `OpeningTree`, from
 * the first `-depth` plies of every game -- for each position reached
 * (identified by its Zobrist/polyglot-style hash, the same one used
 * throughout the codebase for duplicate detection and PQL's fen[] clause),
 * records every move played from it and aggregate stats (game count,
 * win/draw/loss, average Elo, most recent year). Because openings repeat
 * across huge numbers of games, this table stays small relative to the
 * database it was built from.
 *
 * GET /api/tree?fen=... (server.cpp) reads it back: given a position,
 * return the moves played from it, most popular first -- the reference
 * feature every commercial chess database has, that OCGDB otherwise has
 * no equivalent of at all.
 *
 * Purely derived/optional data: dropping OpeningTree changes nothing but
 * that endpoint returning "no opening tree" (see DbRead::hasTable()); it
 * is never required to open or query a database.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#ifndef OCGDB_TREE_H
#define OCGDB_TREE_H

#include <thread>
#include <unordered_map>

#include "dbread.h"

namespace ocgdb {

class OpeningTreeBuilder : public DbRead
{
public:
    virtual void processAGameWithAThread(ThreadRecord* t, const bslib::PgnRecord& record, const std::vector<int8_t>& moveVec) override;

private:
    virtual bool openDB(const std::string& dbPath) override;
    virtual void closeDb() override;
    virtual void runTask() override;
    virtual void printStats() const override;

    struct Agg {
        int64_t games = 0, whiteWin = 0, draw = 0, blackWin = 0, eloSum = 0, eloCnt = 0, lastYear = 0;
    };
    // HashKey -> SAN -> aggregate. One per worker thread while games are
    // being processed (avoids lock contention on the hot per-move path),
    // merged into a single SQLite write pass in closeDb() once the whole
    // scan for a database has finished.
    using EdgeMap = std::unordered_map<int64_t, std::unordered_map<std::string, Agg>>;

    EdgeMap& mapForThread(std::thread::id);
    void mergeAndSave();

private:
    std::mutex perThreadMutex;
    std::unordered_map<std::thread::id, EdgeMap> perThreadAgg;

    int64_t edgeCnt = 0;
};

} // namespace ocgdb

#endif /* OCGDB_TREE_H */
