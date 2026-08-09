/**
 * This file is part of Open Chess Game Database Standard.
 *
 * Implements the `-index` task: creates secondary SQL indexes on Games/
 * Comments so filtering, sorting, and player/event/date lookups (both the
 * CLI and the -server /api/games, /api/stats routes) stop being full table
 * scans -- Builder::createDb() (builder.cpp) creates none at all. Indexes
 * here are purely derived/optional data: dropping them changes nothing but
 * speed, so this task is always safe to run (or skip) on any existing
 * OCGDB database, and safe to re-run (every CREATE INDEX is IF NOT EXISTS).
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#ifndef OCGDB_INDEXER_H
#define OCGDB_INDEXER_H

#include "dbcore.h"

namespace ocgdb {

// Creates the standard set of indexes on whichever Games columns actually
// exist in this database's (dynamically generated, see builder.cpp)
// schema, plus Comments(GameID). Shared between Indexer::runTask() below
// and Builder (builder.cpp), which calls this right after creating a fresh
// database when `-o index` is passed, so a new database can come out of
// -create already indexed without a second pass.
void buildStandardIndexes(SQLite::Database& db, int& createdCnt, int& skippedCnt);

class Indexer : public DbCore
{
public:
    virtual void runTask() override;

private:
    virtual void printStats() const override;

    int createdCnt = 0, skippedCnt = 0;
};

} // namespace ocgdb

#endif /* OCGDB_INDEXER_H */
