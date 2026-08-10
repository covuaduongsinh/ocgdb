/**
 * This file is part of Open Chess Game Database Standard.
 *
 * See jobs.h.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include "jobs.h"

#include <filesystem>
#include <sstream>
#include <regex>
#include <iostream>
#include <set>

using namespace ocgdb;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> splitMulti(const std::string& s, char delim)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string getP(const std::map<std::string, std::string>& params, const char* key)
{
    auto it = params.find(key);
    return it == params.end() ? std::string() : it->second;
}

bool isTruthy(const std::string& s)
{
    return s == "1" || s == "true" || s == "on" || s == "yes";
}

// Whitelists of -o tokens each task is allowed to pass through. Anything
// outside these sets is rejected rather than forwarded -- this is the only
// place a browser-supplied string reaches a child process's command line
// verbatim, so it stays a strict allow-list, not a denylist.
const std::set<std::string> kCreateOpts = {
    "moves", "moves1", "moves2", "acceptnewtags", "discardcomments",
    "discardsites", "discardnoelo", "discardfen", "reseteco", "nobot", "bot", "index", "parseeval",
};
const std::set<std::string> kMergeOpts = { "parseeval" };
const std::set<std::string> kDupOpts = { "remove", "printall", "embededgames" };
const std::set<std::string> kQueryOpts = { "printpgn", "printall", "printfen" };
const std::set<std::string> kOptimizeOpts = { "vacuum", "integrity" };

bool filterOpts(const std::string& csv, const std::set<std::string>& allowed, std::string& out, std::string& err)
{
    out.clear();
    for (auto&& tok : splitMulti(csv, ',')) {
        if (allowed.find(tok) == allowed.end()) {
            err = "unsupported option: " + tok;
            return false;
        }
        if (!out.empty()) out += ',';
        out += tok;
    }
    return true;
}

bool checkPath(const std::function<bool(const std::string&, std::string&)>& filter,
               const std::string& path, std::string& err)
{
    if (!filter) return true;
    return filter(path, err);
}

bool checkExists(const std::string& path, std::string& err)
{
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) {
        err = "path does not exist: " + path;
        return false;
    }
    return true;
}

std::string joinForDisplay(const std::vector<std::string>& argv)
{
    std::ostringstream oss;
    oss << "ocgdb";
    for (auto&& a : argv) {
        oss << ' ';
        bool needQuote = a.empty() || a.find(' ') != std::string::npos;
        if (needQuote) oss << '"' << a << '"'; else oss << a;
    }
    return oss.str();
}

} // namespace

bool ocgdb::buildJobArgv(const std::string& task,
                          const std::map<std::string, std::string>& params,
                          const std::function<bool(const std::string&, std::string&)>& pathFilter,
                          std::vector<std::string>& argv,
                          std::string& paramsText,
                          std::string& writeTargetPath,
                          std::string& err)
{
    argv.clear();
    paramsText.clear();
    writeTargetPath.clear();
    err.clear();

    bool overwrite = isTruthy(getP(params, "overwrite"));

    if (task == "create") {
        auto pgns = splitMulti(getP(params, "pgn"), '|');
        auto db = getP(params, "db");
        auto opts = getP(params, "opts");

        if (pgns.empty()) { err = "at least one PGN file is required"; return false; }
        if (db.empty()) { err = "an output database path is required"; return false; }

        for (auto&& p : pgns) {
            if (!checkPath(pathFilter, p, err)) return false;
            if (!checkExists(p, err)) return false;
        }
        if (!checkPath(pathFilter, db, err)) return false;
        std::error_code ec;
        if (!overwrite && fs::exists(db, ec)) {
            err = "database already exists (set overwrite to replace it): " + db;
            return false;
        }

        std::string optsFiltered;
        if (!opts.empty() && !filterOpts(opts, kCreateOpts, optsFiltered, err)) return false;

        argv.push_back("-create");
        for (auto&& p : pgns) { argv.push_back("-pgn"); argv.push_back(p); }
        argv.push_back("-db"); argv.push_back(db);
        if (!optsFiltered.empty()) { argv.push_back("-o"); argv.push_back(optsFiltered); }
        if (!getP(params, "elo").empty()) { argv.push_back("-elo"); argv.push_back(getP(params, "elo")); }
        if (!getP(params, "plycount").empty()) { argv.push_back("-plycount"); argv.push_back(getP(params, "plycount")); }
        if (!getP(params, "cpu").empty()) { argv.push_back("-cpu"); argv.push_back(getP(params, "cpu")); }
        if (!getP(params, "desc").empty()) { argv.push_back("-desc"); argv.push_back(getP(params, "desc")); }

        writeTargetPath = db;
        paramsText = "create " + db + " from " + std::to_string(pgns.size()) + " PGN file(s)";
        return true;
    }

    if (task == "merge") {
        auto dbDest = getP(params, "dbDest");
        auto dbSources = splitMulti(getP(params, "dbSources"), '|');
        auto pgns = splitMulti(getP(params, "pgn"), '|');

        if (dbDest.empty()) { err = "a destination database path is required"; return false; }
        if (dbSources.empty() && pgns.empty()) { err = "at least one source database or PGN file is required"; return false; }

        if (!checkPath(pathFilter, dbDest, err)) return false;
        if (!checkExists(dbDest, err)) return false;
        for (auto&& p : dbSources) {
            if (!checkPath(pathFilter, p, err)) return false;
            if (!checkExists(p, err)) return false;
        }
        for (auto&& p : pgns) {
            if (!checkPath(pathFilter, p, err)) return false;
            if (!checkExists(p, err)) return false;
        }

        std::string optsFiltered;
        auto opts = getP(params, "opts");
        if (!opts.empty() && !filterOpts(opts, kMergeOpts, optsFiltered, err)) return false;

        argv.push_back("-merge");
        argv.push_back("-db"); argv.push_back(dbDest);
        for (auto&& p : dbSources) { argv.push_back("-db"); argv.push_back(p); }
        for (auto&& p : pgns) { argv.push_back("-pgn"); argv.push_back(p); }
        if (!optsFiltered.empty()) { argv.push_back("-o"); argv.push_back(optsFiltered); }

        writeTargetPath = dbDest;
        paramsText = "merge into " + dbDest;
        return true;
    }

    if (task == "export") {
        auto dbs = splitMulti(getP(params, "db"), '|');
        auto pgnOut = getP(params, "pgnOut");

        if (dbs.empty()) { err = "at least one source database is required"; return false; }
        if (pgnOut.empty()) { err = "an output PGN path is required"; return false; }

        for (auto&& p : dbs) {
            if (!checkPath(pathFilter, p, err)) return false;
            if (!checkExists(p, err)) return false;
        }
        if (!checkPath(pathFilter, pgnOut, err)) return false;
        std::error_code ec;
        if (!overwrite && fs::exists(pgnOut, ec)) {
            err = "output file already exists (set overwrite to replace it): " + pgnOut;
            return false;
        }

        argv.push_back("-export");
        for (auto&& p : dbs) { argv.push_back("-db"); argv.push_back(p); }
        argv.push_back("-pgn"); argv.push_back(pgnOut);

        // Read-only w.r.t. any registered .ocgdb.db3 -- writeTargetPath stays
        // empty so exporting never closes the active browsing connection.
        paramsText = "export " + std::to_string(dbs.size()) + " database(s) to " + pgnOut;
        return true;
    }

    if (task == "dup") {
        auto dbs = splitMulti(getP(params, "db"), '|');
        if (dbs.empty()) { err = "at least one database is required"; return false; }
        for (auto&& p : dbs) {
            if (!checkPath(pathFilter, p, err)) return false;
            if (!checkExists(p, err)) return false;
        }

        bool remove = isTruthy(getP(params, "remove"));
        if (remove && !isTruthy(getP(params, "confirm"))) {
            err = "removing duplicates modifies the database in place; confirm is required";
            return false;
        }

        std::string opts;
        if (remove) opts = "remove";
        if (isTruthy(getP(params, "printall"))) opts += (opts.empty() ? "" : ",") + std::string("printall");

        argv.push_back("-dup");
        for (auto&& p : dbs) { argv.push_back("-db"); argv.push_back(p); }
        if (!opts.empty()) { argv.push_back("-o"); argv.push_back(opts); }
        if (!getP(params, "report").empty()) {
            if (!checkPath(pathFilter, getP(params, "report"), err)) return false;
            argv.push_back("-r"); argv.push_back(getP(params, "report"));
        }

        if (remove && dbs.size() == 1) writeTargetPath = dbs.front();
        paramsText = std::string(remove ? "remove duplicates in " : "check duplicates in ") +
                     std::to_string(dbs.size()) + " database(s)";
        return true;
    }

    if (task == "index") {
        auto dbs = splitMulti(getP(params, "db"), '|');
        if (dbs.empty()) { err = "at least one database is required"; return false; }
        for (auto&& p : dbs) {
            if (!checkPath(pathFilter, p, err)) return false;
            if (!checkExists(p, err)) return false;
        }

        argv.push_back("-index");
        for (auto&& p : dbs) { argv.push_back("-db"); argv.push_back(p); }

        // Only close/reopen WebServer's own read connection (see
        // onJobStart/onJobEnd, server.cpp) when there is exactly one
        // unambiguous target, same convention as "dup" above.
        if (dbs.size() == 1) writeTargetPath = dbs.front();
        paramsText = "build indexes on " + std::to_string(dbs.size()) + " database(s)";
        return true;
    }

    if (task == "optimize") {
        auto dbs = splitMulti(getP(params, "db"), '|');
        if (dbs.empty()) { err = "at least one database is required"; return false; }
        for (auto&& p : dbs) {
            if (!checkPath(pathFilter, p, err)) return false;
            if (!checkExists(p, err)) return false;
        }

        std::string optsFiltered;
        auto opts = getP(params, "opts");
        if (!opts.empty() && !filterOpts(opts, kOptimizeOpts, optsFiltered, err)) return false;

        argv.push_back("-optimize");
        for (auto&& p : dbs) { argv.push_back("-db"); argv.push_back(p); }
        if (!optsFiltered.empty()) { argv.push_back("-o"); argv.push_back(optsFiltered); }

        if (dbs.size() == 1) writeTargetPath = dbs.front();
        paramsText = "optimize " + std::to_string(dbs.size()) + " database(s)" +
                     (optsFiltered.empty() ? std::string() : " (" + optsFiltered + ")");
        return true;
    }

    if (task == "material") {
        auto dbs = splitMulti(getP(params, "db"), '|');
        if (dbs.empty()) { err = "at least one database is required"; return false; }
        for (auto&& p : dbs) {
            if (!checkPath(pathFilter, p, err)) return false;
            if (!checkExists(p, err)) return false;
        }

        argv.push_back("-material");
        for (auto&& p : dbs) { argv.push_back("-db"); argv.push_back(p); }

        if (dbs.size() == 1) writeTargetPath = dbs.front();
        paramsText = "build material pre-filter on " + std::to_string(dbs.size()) + " database(s)";
        return true;
    }

    if (task == "tree") {
        auto dbs = splitMulti(getP(params, "db"), '|');
        if (dbs.empty()) { err = "at least one database is required"; return false; }
        for (auto&& p : dbs) {
            if (!checkPath(pathFilter, p, err)) return false;
            if (!checkExists(p, err)) return false;
        }

        argv.push_back("-tree");
        for (auto&& p : dbs) { argv.push_back("-db"); argv.push_back(p); }
        auto depth = getP(params, "depth");
        if (!depth.empty()) { argv.push_back("-depth"); argv.push_back(depth); }

        if (dbs.size() == 1) writeTargetPath = dbs.front();
        paramsText = "build opening tree on " + std::to_string(dbs.size()) + " database(s)" +
                     (depth.empty() ? std::string() : " (depth " + depth + ")");
        return true;
    }

    if (task == "bench") {
        auto db = getP(params, "db");
        if (db.empty()) { err = "a database path is required"; return false; }
        if (!checkPath(pathFilter, db, err)) return false;
        if (!checkExists(db, err)) return false;

        argv.push_back("-bench");
        argv.push_back("-db"); argv.push_back(db);
        if (!getP(params, "cpu").empty()) { argv.push_back("-cpu"); argv.push_back(getP(params, "cpu")); }

        paramsText = "benchmark " + db;
        return true;
    }

    if (task == "query") {
        auto db = getP(params, "db");
        auto pql = getP(params, "pql");
        if (db.empty()) { err = "a database path is required"; return false; }
        if (pql.empty()) { err = "a PQL query is required"; return false; }
        if (!checkPath(pathFilter, db, err)) return false;
        if (!checkExists(db, err)) return false;

        // "printall" is always forced on (not just when the user picks it)
        // so every match prints a parseable "N. gameId: ID" line -- see
        // JobManager::runOne()'s kGameIdRe, jobs.cpp, which reads exactly
        // that format to persist structured results into AdminStore's
        // QueryResults table (Phase 2.5) as the job runs, independent of
        // whatever the user separately picked for on-screen log output
        // (printpgn also dumping full PGN text into the log).
        std::string opts = "printall";
        if (getP(params, "printFormat") == "printpgn") opts += ",printpgn";

        argv.push_back("-db"); argv.push_back(db);
        argv.push_back("-q"); argv.push_back(pql);
        argv.push_back("-o"); argv.push_back(opts);
        if (!getP(params, "report").empty()) {
            if (!checkPath(pathFilter, getP(params, "report"), err)) return false;
            argv.push_back("-r"); argv.push_back(getP(params, "report"));
        }

        paramsText = "query \"" + pql + "\" on " + db;
        return true;
    }

    err = "unknown task: " + task;
    return false;
}

// ============================================================ JobManager

JobManager::~JobManager()
{
    stop();
}

void JobManager::start(AdminStore* storeIn, const std::string& exePathIn)
{
    store = storeIn;
    exePath = exePathIn;
    shuttingDown = false;
    worker = std::thread(&JobManager::workerLoop, this);
}

void JobManager::stop()
{
    shuttingDown = true;
    {
        std::lock_guard<std::mutex> lk(childMutex);
        if (activeChild) activeChild->terminate();
    }
    cv.notify_all();
    if (worker.joinable()) worker.join();
}

int JobManager::submit(const std::string& task, const std::map<std::string, std::string>& params, std::string& err)
{
    QueuedJob job;
    job.task = task;
    std::string paramsText;
    if (!buildJobArgv(task, params, pathFilter, job.argv, paramsText, job.writeTargetPath, err)) {
        return -1;
    }
    // Every job runs with -progress so runOne() can parse "@@PROGRESS ..."
    // lines out of its output (see core.cpp); harmless for tasks that don't
    // emit any.
    job.argv.push_back("-progress");

    auto cmdline = joinForDisplay(job.argv);
    auto id = store ? store->createJob(task, paramsText, cmdline) : -1;
    if (id < 0) {
        err = "could not persist job";
        return -1;
    }
    job.id = id;

    {
        std::lock_guard<std::mutex> lk(queueMutex);
        queued[id] = job;
    }
    cv.notify_one();
    return id;
}

bool JobManager::cancel(int jobId)
{
    {
        std::lock_guard<std::mutex> lk(queueMutex);
        auto it = queued.find(jobId);
        if (it != queued.end()) {
            queued.erase(it);
            if (store) store->cancelQueuedJob(jobId);
            return true;
        }
    }
    if (activeJobId.load() == jobId) {
        std::lock_guard<std::mutex> lk(childMutex);
        if (activeChild) {
            activeChild->terminate();
            return true;
        }
    }
    return false;
}

void JobManager::workerLoop()
{
    for (;;) {
        QueuedJob job;
        {
            std::unique_lock<std::mutex> qlk(queueMutex);
            cv.wait(qlk, [&] { return shuttingDown.load() || !queued.empty(); });
            if (queued.empty()) {
                if (shuttingDown) return;
                continue;
            }
            auto it = queued.begin(); // smallest job ID == oldest submission
            job = it->second;
            queued.erase(it);
        }
        runOne(job);
    }
}

void JobManager::runOne(const QueuedJob& job)
{
    activeJobId = job.id;
    if (store) store->setJobRunning(job.id);
    if (!job.writeTargetPath.empty() && onJobStart) onJobStart(job.writeTargetPath);

    static const std::regex kProgressRe(
        R"(@@PROGRESS\s+games=(\d+)(?:\s+elapsed=\d+)?(?:\s+bytes=(\d+))?(?:\s+total=(\d+))?)");
    static const std::regex kStatsRe(R"(#games:\s*(\d+))");
    // Matches Search::runTask()'s per-match line (search.cpp, gated on the
    // "printall" option this job's argv always includes for task=="query"
    // -- see buildJobArgv() above): "<N>. gameId: <ID>".
    static const std::regex kGameIdRe(R"(^\d+\.\s+gameId:\s+(-?\d+)\s*$)");
    const bool isQueryJob = (job.task == "query");
    // Buffered and flushed periodically rather than one INSERT (and one
    // AdminStore mutex lock) per match -- a broad, unfiltered PQL scan can
    // match hundreds of thousands of games, exactly the case this exists
    // to support well.
    std::vector<int> pendingResults;
    const size_t kFlushEvery = 5000;
    auto flushResults = [&]() {
        if (!pendingResults.empty() && store) {
            store->addQueryResults(job.id, pendingResults);
            pendingResults.clear();
        }
    };

    auto child = new ChildProcess();
    {
        std::lock_guard<std::mutex> lk(childMutex);
        activeChild = child;
    }

    bool started = child->start(exePath, job.argv);
    std::string state = "failed";
    int exitCode = -1;
    std::string errText;

    if (!started) {
        errText = child->errorString;
        if (store) store->appendLog(job.id, "Error: failed to start process: " + errText);
    } else {
        std::string line;
        int64_t lastGames = 0, lastBytes = 0, lastTotal = 0;
        while (child->readLine(line)) {
            std::smatch m;

            // A query job's "printall" output is one line per match --
            // for a broad scan that can be hundreds of thousands of
            // lines. Route those into the batched QueryResults path below
            // instead of appendLog(), which does one synchronous INSERT
            // (its own implicit transaction) per line: appending all of
            // them made a query that finishes in ~150ms as `-q` on the
            // command line take upwards of 40s as a job, almost entirely
            // JobLog insert overhead for data QueryResults already
            // captures far more cheaply. Every other line (headers,
            // @@PROGRESS, the final summary) still goes to the log as
            // before -- this only skips the redundant, high-volume case.
            bool isGameIdLine = isQueryJob && std::regex_search(line, m, kGameIdRe);
            if (!isGameIdLine && store) store->appendLog(job.id, line);

            if (isGameIdLine) {
                auto gameId = std::stoi(m[1].str());
                if (gameId > 0) {
                    pendingResults.push_back(gameId);
                    if (pendingResults.size() >= kFlushEvery) flushResults();
                }
            } else if (std::regex_search(line, m, kProgressRe)) {
                lastGames = std::stoll(m[1].str());
                if (m[2].matched) lastBytes = std::stoll(m[2].str());
                if (m[3].matched) lastTotal = std::stoll(m[3].str());
                if (store) store->setJobProgress(job.id, lastBytes, lastTotal, lastGames);
            } else if (std::regex_search(line, m, kStatsRe)) {
                lastGames = std::stoll(m[1].str());
                if (store) store->setJobProgress(job.id, lastBytes, lastTotal, lastGames);
            }
        }
        flushResults();
        exitCode = child->wait();
        state = (exitCode == 0) ? "succeeded" : "failed";
        if (exitCode != 0) errText = "process exited with code " + std::to_string(exitCode);
    }

    {
        std::lock_guard<std::mutex> lk(childMutex);
        // A concurrent cancel() may have called terminate() on this exact
        // child; ChildProcess::wait() already reflects that as exitCode -1,
        // so only relabel the *state* here (terminate() cannot itself know
        // whether it raced a natural finish).
        if (activeChild == child) activeChild = nullptr;
    }
    delete child;

    bool wasCancelled = (exitCode == -1 && started);
    if (wasCancelled) state = "cancelled";

    if (store) store->finishJob(job.id, state, exitCode, errText);
    activeJobId = -1;

    if (!job.writeTargetPath.empty() && onJobEnd) onJobEnd(job.writeTargetPath);
}
