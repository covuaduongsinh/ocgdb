/**
 * This file is part of Open Chess Game Database Standard.
 *
 * Implements the `-material` task: for every game, scans every position
 * (the starting position plus every ply played) and records the maximum
 * number of each piece type/color reached at any point into a derived
 * table, `GameMaterial`. This lets a PQL query that imposes a simple
 * minimum piece count (e.g. "Q=3") skip games that could never match
 * without replaying a single move -- see Parser::buildMaterialPreFilterSql()
 * (parser.h/.cpp) for how a parsed query becomes a WHERE clause over this
 * table, and search.cpp/server.cpp for where that gets applied to a scan.
 *
 * GameMaterial is purely derived data: dropping it changes nothing but
 * speed (queries simply stop being pre-filtered and fall back to a full
 * scan, exactly like before this existed), so this task is always safe to
 * run, skip, or re-run. It always rebuilds the table from scratch (DROP +
 * CREATE), so it must be re-run after any database mutation (-merge, -dup
 * -o remove, ...) for the pre-filter to reflect the current data -- an
 * out-of-date GameMaterial can only make a query wrongly SKIP a game that
 * used to not exist, not wrongly match a nonexistent one, but it should
 * still be re-run to stay meaningful.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#ifndef OCGDB_MATERIAL_H
#define OCGDB_MATERIAL_H

#include <thread>
#include <unordered_map>

#include "dbread.h"

namespace ocgdb {

class MaterialBuilder : public DbRead
{
public:
    virtual void processAGameWithAThread(ThreadRecord* t, const bslib::PgnRecord& record, const std::vector<int8_t>& moveVec) override;

private:
    virtual bool openDB(const std::string& dbPath) override;
    virtual void closeDb() override;
    virtual void runTask() override;
    virtual void printStats() const override;

    SQLite::Statement* insertStatementFor(std::thread::id);

private:
    std::mutex insertStmtMapMutex;
    std::unordered_map<std::thread::id, SQLite::Statement*> insertStmtMap;
};

} // namespace ocgdb

#endif /* OCGDB_MATERIAL_H */
