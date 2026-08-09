/**
 * This file is part of Open Chess Game Database Standard.
 *
 * Implements the `-optimize` task: routine SQLite maintenance for an
 * existing OCGDB database.
 *  - ANALYZE always runs: cheap, and it's what lets SQLite's query planner
 *    actually pick the indexes Indexer (see indexer.h) creates instead of
 *    still falling back to a full table scan.
 *  - PRAGMA optimize always runs: SQLite's own lightweight "is there
 *    anything cheap worth doing" pass; a safe no-op most of the time.
 *  - VACUUM (-o vacuum) is opt-in: rewrites the entire file, needs roughly
 *    the database's current size again in free disk space, and holds an
 *    exclusive lock for the duration. Mainly useful after "-dup -o remove"
 *    leaves free pages the file doesn't shrink to give back on its own.
 *  - PRAGMA integrity_check (-o integrity) is opt-in: a full read of every
 *    page, only worth paying for when corruption is suspected (e.g. after
 *    a crash -- Builder::createDb() runs with journal_mode=OFF, see
 *    builder.cpp, which trades crash-safety for write speed).
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#ifndef OCGDB_OPTIMIZER_H
#define OCGDB_OPTIMIZER_H

#include "dbcore.h"

namespace ocgdb {

class Optimizer : public DbCore
{
public:
    virtual void runTask() override;

private:
    virtual void printStats() const override;

    void optimizeOneDb(const std::string& dbPath);

    int okCnt = 0, failCnt = 0;
    std::vector<std::string> integrityIssues;
};

} // namespace ocgdb

#endif /* OCGDB_OPTIMIZER_H */
