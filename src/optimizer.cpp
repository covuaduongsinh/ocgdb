/**
 * This file is part of Open Chess Game Database Standard.
 *
 * See optimizer.h.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include <chrono>
#include <iostream>

#include "optimizer.h"

using namespace ocgdb;

void Optimizer::runTask()
{
    std::cout << "Optimizing databases..." << std::endl;

    startTime = getNow();
    okCnt = failCnt = 0;
    integrityIssues.clear();

    for (auto&& dbPath : paraRecord.dbPaths) {
        optimizeOneDb(dbPath);
    }

    printStats();
}

void Optimizer::optimizeOneDb(const std::string& dbPath)
{
    std::cout << "DB path: " << dbPath << std::endl;

    try {
        SQLite::Database db(dbPath, SQLite::OPEN_READWRITE);

        if (paraRecord.optionFlag & optimize_flag_integrity) {
            std::cout << "  integrity_check..." << std::endl;
            SQLite::Statement stmt(db, "PRAGMA integrity_check");
            while (stmt.executeStep()) {
                auto s = stmt.getColumn(0).getString();
                if (s != "ok") {
                    integrityIssues.push_back(dbPath + ": " + s);
                    std::cerr << "  INTEGRITY ISSUE: " << s << std::endl;
                }
            }
        }

        std::cout << "  ANALYZE..." << std::endl;
        db.exec("ANALYZE");

        if (paraRecord.optionFlag & optimize_flag_vacuum) {
            std::cout << "  VACUUM..." << std::endl;
            db.exec("VACUUM");
        }

        db.exec("PRAGMA optimize");

        okCnt++;
    } catch (std::exception& e) {
        std::cerr << "Error: could not optimize database " << dbPath << ": " << e.what() << std::endl;
        failCnt++;
        errCnt++;
    }
}

void Optimizer::printStats() const
{
    int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(getNow() - startTime).count() + 1;

    std::lock_guard<std::mutex> dolock(printMutex);
    std::cout << "#databases optimized: " << okCnt << ", failed: " << failCnt
               << ", elapsed: " << elapsed << "ms";
    if (!integrityIssues.empty()) {
        std::cout << ", integrity issues found: " << integrityIssues.size();
    }
    std::cout << std::endl;
}
