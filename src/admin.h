/**
 * This file is part of Open Chess Game Database Standard.
 *
 * AdminStore is the persistent state behind the -server control plane: the
 * set of .ocgdb.db3 files the user has registered, the job queue/history
 * (create/merge/export/dup/bench/query runs launched as child processes,
 * see process.h and jobs.h), and a couple of small settings such as which
 * database is "active" (the one /api/games, /api/query etc. read from).
 *
 * Backed by its own small SQLite file, separate from any .ocgdb.db3 game
 * database -- this store's schema has nothing to do with the OCGDB game
 * schema. One SQLite::Database connection plus one mutex guarding every
 * method body; traffic here is tiny compared to the game databases
 * WebServer otherwise serves, so a single global lock is simple and correct
 * rather than a bottleneck worth avoiding.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#ifndef OCGDB_ADMIN_H
#define OCGDB_ADMIN_H

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

#include "3rdparty/SQLiteCpp/SQLiteCpp.h"

namespace ocgdb {

struct DbEntry {
    int id = -1;
    std::string path;
    std::string label;
    int64_t addedAt = 0;
    int64_t lastOpenedAt = 0;
    std::string note; // e.g. "incomplete" if a create/merge job targeting it was cancelled
};

struct JobEntry {
    int id = -1;
    std::string task;         // create | merge | export | dup | bench | query
    std::string paramsText;   // human-readable summary, shown in the UI
    std::string cmdline;      // the exact argv, joined for display (never re-parsed)
    std::string state;        // queued | running | succeeded | failed | cancelled
    int64_t progress = 0;
    int64_t progressTotal = 0;
    int64_t gameCnt = 0;
    int exitCode = 0;
    std::string error;
    int64_t createdAt = 0;
    int64_t startedAt = 0;
    int64_t finishedAt = 0;
};

struct JobLogLine {
    int64_t seq = 0;
    std::string line;
};

class AdminStore
{
public:
    // dbPath: path to the admin store's own SQLite file (NOT a game
    // database). Creates the file and schema if missing.
    explicit AdminStore(const std::string& dbPath);
    ~AdminStore();

    bool isOpen() const { return db != nullptr; }

    // ---- Databases -------------------------------------------------
    // Registers a path (no-op, returns the existing row, if already
    // registered -- Path is UNIQUE).
    DbEntry addDatabase(const std::string& path, const std::string& label);
    bool removeDatabase(int id);
    std::vector<DbEntry> listDatabases() const;
    bool getDatabase(int id, DbEntry& out) const;
    bool findDatabaseByPath(const std::string& path, DbEntry& out) const;
    void touchDatabaseOpened(int id);
    void setDatabaseNote(int id, const std::string& note);

    // ---- Settings ----------------------------------------------------
    std::string getSetting(const std::string& name, const std::string& def = "") const;
    void setSetting(const std::string& name, const std::string& value);

    // ---- Jobs ----------------------------------------------------------
    int createJob(const std::string& task, const std::string& paramsText, const std::string& cmdline);
    void setJobRunning(int jobId);
    void setJobProgress(int jobId, int64_t progress, int64_t progressTotal, int64_t gameCnt);
    void finishJob(int jobId, const std::string& state, int exitCode, const std::string& error);
    bool cancelQueuedJob(int jobId); // only affects jobs still in 'queued' state
    std::vector<JobEntry> listJobs(const std::string& stateFilter = "") const;
    bool getJob(int jobId, JobEntry& out) const;
    // Oldest still-queued job, if any (used by the worker loop).
    bool nextQueuedJob(JobEntry& out) const;
    void clearFinishedJobs();

    // ---- Job log ---------------------------------------------------
    void appendLog(int jobId, const std::string& line);
    std::vector<JobLogLine> getLog(int jobId, int64_t fromSeq) const;

    // ---- Query job results (Phase 2.5) ------------------------------
    // Structured GameID matches for a "query" task job, parsed by
    // JobManager::runOne() (jobs.cpp) out of the child process's "N.
    // gameId: ID" lines as they stream in -- kept separately from JobLog,
    // which only retains the most recent kMaxLogLinesPerJob lines (a
    // rolling window unsuitable for "every match", which is the whole
    // point of running a PQL query as a job instead of the synchronous
    // /api/query in the first place). Never trimmed by count, only ever
    // deleted alongside the owning job (clearFinishedJobs() below).
    void addQueryResults(int jobId, const std::vector<int>& gameIds);
    std::vector<int> getQueryResults(int jobId, int64_t offset, int64_t limit) const;
    int64_t countQueryResults(int jobId) const;

private:
    void execSql(const std::string& sql);
    static int64_t nowEpoch();

    mutable std::mutex mutex_;
    SQLite::Database* db = nullptr;

    static const int64_t kMaxLogLinesPerJob = 5000;
};

} // namespace ocgdb

#endif /* OCGDB_ADMIN_H */
