/**
 * This file is part of Open Chess Game Database Standard.
 *
 * Implements the `-server` task: a local HTTP control plane (cpp-httplib,
 * src/3rdparty/httplib/httplib.h) that
 *  (a) exposes a JSON API for browsing ONE "active" OCGDB database (used by
 *      the web UI in web/) plus static hosting for that UI, and
 *  (b) runs OCGDB's own CLI tasks (create/merge/export/dup/bench/query) as
 *      queued, cancellable background jobs via JobManager (jobs.h), and
 *      lets the active database be switched at runtime among a set the
 *      user has registered via AdminStore (admin.h).
 *
 * Several independent pieces of state, each with its own concurrency story:
 *  - `active.db`, the persistent read-only connection for whichever
 *    database is currently being browsed. Guarded by `dbMutex`
 *    (shared_mutex): every "light" /api/* route takes a shared lock (via
 *    tryReadLock(), failing fast with 503 rather than queuing behind a
 *    write) so reads run concurrently with each other; switching databases
 *    or a job about to write to the active one takes an exclusive lock.
 *    SQLite itself is compiled SQLITE_THREADSAFE=1 ("serialized"), so the
 *    one connection may additionally be used from many threads at once as
 *    long as each creates its own SQLite::Statement, which every handler
 *    below does.
 *  - the inherited DbRead::readADb() opens/closes its own connection
 *    (assigned to the inherited `mDb`) each time a PQL query runs. That
 *    path also drives the shared, static Core::pool, so `pqlMutex`
 *    serializes /api/query requests to keep mDb/searchField/gameCnt from
 *    being touched by two requests at once. /api/query still takes
 *    dbMutex (shared) for the duration, since it also reads `active.*` for
 *    the scan-size check and the final per-hit detail queries.
 *  - `adminStore` (its own small SQLite file, admin.h) and `jobManager`
 *    (jobs.h) manage their own internal locking independently of dbMutex.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <random>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <thread>
#include <tuple>
#include <unordered_map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "3rdparty/httplib/httplib.h"

#include "server.h"
#include "json.h"
#include "process.h"
#include "board/funcs.h"

using namespace ocgdb;

namespace {

// Query-string parameters accepted by /api/games. Whitelisted explicitly
// (rather than iterating every param httplib parsed) so it's obvious at a
// glance what the endpoint understands.
const char* GamesParamNames[] = {
    "offset", "limit", "white", "black", "player", "event", "site",
    "result", "eco", "minElo", "maxElo", "dateFrom", "dateTo", "minPly",
    "sort", "dir", nullptr
};

// Form-encoded params accepted by the various /api/admin/jobs/submit
// tasks. Whitelisted the same way as GamesParamNames above; buildJobArgv()
// (jobs.cpp) additionally validates/rejects the *values*, this list just
// controls which keys are even looked at.
const char* JobParamNames[] = {
    "task", "pgn", "db", "opts", "elo", "plycount", "cpu", "desc",
    "overwrite", "dbDest", "dbSources", "pgnOut", "remove", "confirm",
    "printall", "report", "pql", "printFormat", "depth", nullptr
};

std::string randomHexToken(size_t bytes)
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    std::ostringstream oss;
    for (size_t i = 0; i < bytes; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << dist(gen);
    }
    return oss.str();
}

// Constant-time-ish comparison so a mistyped/probing token doesn't leak
// timing information proportional to how many leading bytes matched.
bool constantTimeEquals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); i++) diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

// Body for any "light" /api/* route when tryReadLock() fails -- a
// background job (see JobManager) currently owns the active database.
std::string busyJsonBody(int jobId)
{
    Json j;
    j.objBegin();
    j.kv("busy", true);
    j.kv("error", "the active database is being written by a background job");
    if (jobId >= 0) j.kv("jobId", jobId); else j.kvNull("jobId");
    j.objEnd();
    return j.str();
}

void writeJobEntry(Json& j, const JobEntry& e)
{
    j.objBegin();
    j.kv("id", e.id);
    j.kv("task", e.task);
    j.kv("paramsText", e.paramsText);
    j.kv("cmdline", e.cmdline);
    j.kv("state", e.state);
    j.kv("progress", e.progress);
    j.kv("progressTotal", e.progressTotal);
    j.kv("gameCnt", e.gameCnt);
    j.kv("exitCode", e.exitCode);
    j.kv("error", e.error);
    j.kv("createdAt", e.createdAt);
    j.kv("startedAt", e.startedAt);
    j.kv("finishedAt", e.finishedAt);
    j.objEnd();
}

} // namespace

WebServer::~WebServer()
{
    if (jobManager) {
        jobManager->stop();
        delete jobManager;
        jobManager = nullptr;
    }
    {
        std::unique_lock<std::shared_mutex> lock(dbMutex);
        closeActiveDbLocked();
    }
    delete adminStore;
    adminStore = nullptr;
}

std::string WebServer::findWebDir() const
{
    namespace fs = std::filesystem;

    if (!paraRecord.webDir.empty()) {
        if (fs::exists(paraRecord.webDir) && fs::is_directory(paraRecord.webDir)) {
            return paraRecord.webDir;
        }
        std::cerr << "Warning: -web directory not found: " << paraRecord.webDir << std::endl;
    }

    if (fs::exists("web") && fs::is_directory("web")) {
        return "web";
    }

    return std::string();
}

std::vector<std::string> WebServer::loadGamesColumnsFor(SQLite::Database* db)
{
    std::vector<std::string> cols;
    if (!db) return cols;
    SQLite::Statement stmt(*db, "PRAGMA table_info(Games)");
    while (stmt.executeStep()) {
        cols.push_back(stmt.getColumn(1).getString());
    }
    return cols;
}

bool WebServer::hasGamesColumn(const std::string& name) const
{
    return std::find(active.gamesColumns.begin(), active.gamesColumns.end(), name) != active.gamesColumns.end();
}

////////////////////////////////////////////////////////////////////////////
// Active-database lifecycle. openActiveDbLocked/closeActiveDbLocked assume
// the caller already holds dbMutex exclusively; activateDatabase() and the
// JobManager callbacks below take it themselves.

bool WebServer::openActiveDbLocked(const std::string& path, std::string& err)
{
    err.clear();
    try {
        auto db = new SQLite::Database(path, SQLite::OPEN_READONLY);
        auto sf = DbRead::getMoveField(db);
        if (sf == SearchField::none) {
            delete db;
            err = "database has no move field (Moves/Moves1/Moves2): " + path;
            active.db = nullptr;
            active.path = path;
            active.searchField = SearchField::none;
            active.gamesColumns.clear();
            active.statsCache.clear();
            return false;
        }
        active.db = db;
        active.path = path;
        active.searchField = sf;
        active.gamesColumns = loadGamesColumnsFor(db);
        active.statsCache.clear();
        return true;
    } catch (SQLite::Exception& e) {
        err = std::string("could not open database: ") + e.what();
        active.db = nullptr;
        active.path = path;
        active.searchField = SearchField::none;
        active.gamesColumns.clear();
        active.statsCache.clear();
        return false;
    }
}

void WebServer::closeActiveDbLocked()
{
    delete active.db;
    active.db = nullptr;
    active.gamesColumns.clear();
    active.statsCache.clear();
    // active.path is deliberately kept: onJobEndReopenIfActive() needs it
    // to know what to reopen, and the admin UI wants to show which path is
    // (still) selected even while it's momentarily closed for a job.
}

bool WebServer::activateDatabase(const std::string& path, std::string& err)
{
    std::unique_lock<std::shared_mutex> lock(dbMutex);
    closeActiveDbLocked();
    auto ok = openActiveDbLocked(path, err);
    if (adminStore) {
        adminStore->setSetting("activeDbPath", path);
        if (ok) {
            DbEntry e;
            if (adminStore->findDatabaseByPath(path, e)) adminStore->touchDatabaseOpened(e.id);
        }
    }
    return ok;
}

void WebServer::onJobStartCloseIfActive(const std::string& path)
{
    std::unique_lock<std::shared_mutex> lock(dbMutex);
    if (active.path == path) {
        closeActiveDbLocked();
    }
}

void WebServer::onJobEndReopenIfActive(const std::string& path)
{
    std::unique_lock<std::shared_mutex> lock(dbMutex);
    if (active.path == path && active.db == nullptr) {
        std::string err;
        if (!openActiveDbLocked(path, err)) {
            std::cerr << "Warning: could not reopen active database after job finished: " << err << std::endl;
        }
    }
}

bool WebServer::tryReadLock(std::shared_lock<std::shared_mutex>& lock) const
{
    std::shared_lock<std::shared_mutex> l(dbMutex, std::try_to_lock);
    if (!l.owns_lock()) return false;
    lock = std::move(l);
    return true;
}

////////////////////////////////////////////////////////////////////////////
// Admin auth / path safety

bool WebServer::checkAdminAuth(const httplib::Request& req, httplib::Response& res) const
{
    auto reqToken = req.get_header_value("X-OCGDB-Token");
    if (adminToken.empty() || !constantTimeEquals(reqToken, adminToken)) {
        res.status = 401;
        Json j; j.objBegin().kv("error", "missing or invalid X-OCGDB-Token").objEnd();
        res.set_content(j.str(), "application/json; charset=utf-8");
        return false;
    }
    // Browsers only attach an Origin header on requests that could be
    // cross-origin; a same-origin fetch from the served web UI may omit it
    // entirely, so only reject when it's present AND doesn't match -- this
    // is the CSRF backstop, the token above is the primary control.
    auto origin = req.get_header_value("Origin");
    if (!origin.empty()) {
        auto expected = "http://127.0.0.1:" + std::to_string(paraRecord.port);
        if (origin != expected) {
            res.status = 403;
            Json j; j.objBegin().kv("error", "cross-origin request rejected").objEnd();
            res.set_content(j.str(), "application/json; charset=utf-8");
            return false;
        }
    }
    return true;
}

bool WebServer::pathAllowed(const std::string& path, std::string& err) const
{
    namespace fs = std::filesystem;
    if (path.empty()) { err = "empty path"; return false; }

    // weakly_canonical resolves as much of the path as exists on disk
    // without requiring the final component to exist yet (needed for e.g.
    // a -create output path, which by definition doesn't exist), and still
    // normalizes away "." / ".." along the way.
    std::error_code ec;
    auto normalized = fs::weakly_canonical(fs::path(path), ec);
    if (ec) { err = "invalid path: " + path; return false; }

    if (paraRecord.rootDir.empty()) return true;

    auto root = fs::weakly_canonical(fs::path(paraRecord.rootDir), ec);
    if (ec) { err = "invalid -root directory"; return false; }

    // Compare as forward-slash-normalized UTF-8 so this works identically
    // on Windows (native separator '\\') and POSIX ('/').
    auto slashify = [](std::string s) {
        for (auto& c : s) if (c == '\\') c = '/';
        return s;
    };
    auto rootStr = slashify(root.u8string());
    auto pathStr = slashify(normalized.u8string());
    if (pathStr.size() < rootStr.size() ||
        pathStr.compare(0, rootStr.size(), rootStr) != 0 ||
        (pathStr.size() > rootStr.size() && pathStr[rootStr.size()] != '/')) {
        err = "path is outside -root: " + path;
        return false;
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////

void WebServer::runTask()
{
    std::cout << "Starting web server..." << std::endl;
    startTime = getNow();

    if (paraRecord.dbPaths.empty()) {
        std::cerr << "Error: -server requires at least one database path (-db)" << std::endl;
        return;
    }

    namespace fs = std::filesystem;

    // ---- admin store: registered databases, job queue/history ---------
    auto adminDbPath = paraRecord.adminDbPath;
    if (adminDbPath.empty()) {
        auto exe = getExecutablePath();
        adminDbPath = !exe.empty() ? (fs::path(exe).parent_path() / "ocgdb-admin.db3").string()
                                    : std::string("ocgdb-admin.db3");
    }
    adminStore = new AdminStore(adminDbPath);
    if (!adminStore->isOpen()) {
        std::cerr << "Error: could not open/create admin store at " << adminDbPath << std::endl;
        return;
    }
    for (auto&& p : paraRecord.dbPaths) {
        adminStore->addDatabase(p, fs::path(p).filename().string());
    }

    // Prefer whichever database was active the last time the server ran,
    // if it's still registered and still on disk; otherwise the first -db.
    std::string startPath;
    auto remembered = adminStore->getSetting("activeDbPath");
    if (!remembered.empty()) {
        DbEntry e;
        std::error_code ec;
        if (adminStore->findDatabaseByPath(remembered, e) && fs::exists(remembered, ec)) {
            startPath = remembered;
        }
    }
    if (startPath.empty()) startPath = paraRecord.dbPaths.front();

    std::string activateErr;
    if (!activateDatabase(startPath, activateErr)) {
        // Soft failure, by design: the control plane should still come up
        // so the Admin tab can be used to register/activate a working
        // database, rather than the whole process refusing to start.
        std::cerr << "Warning: could not open initial database (" << activateErr
                  << "); use the Admin tab to register/activate one." << std::endl;
    }

    // ---- job manager: runs create/merge/export/dup/bench/query as -----
    // ---- queued child processes (see jobs.h/process.h) -----------------
    jobManager = new JobManager();
    jobManager->pathFilter = [this](const std::string& p, std::string& e) { return pathAllowed(p, e); };
    jobManager->onJobStart = [this](const std::string& p) { onJobStartCloseIfActive(p); };
    jobManager->onJobEnd = [this](const std::string& p) { onJobEndReopenIfActive(p); };
    auto exePath = getExecutablePath();
    if (exePath.empty()) {
        std::cerr << "Warning: could not determine this program's own executable path; "
                      "background jobs will fail to start until this is fixed." << std::endl;
    }
    jobManager->start(adminStore, exePath);

    // ---- admin token: required on every /api/admin/* request ----------
    adminToken = paraRecord.adminToken;
    if (adminToken.empty()) adminToken = randomHexToken(16);
    {
        auto tokenFile = fs::path(adminDbPath).parent_path() / "admin-token.txt";
        std::ofstream f(tokenFile, std::ios::trunc);
        if (f) f << adminToken << std::endl;
    }

    auto webDir = findWebDir();
    if (webDir.empty()) {
        std::cerr << "Error: could not find the web UI folder. Pass -web <dir> to point at it "
                      "(default search: -web value, then ./web)." << std::endl;
        return;
    }

    httplib::Server server;
    svr = &server;

    // Default is 8KB (fine for every JSON/form route here); raised for
    // /api/admin/upload, which needs to accept whole PGN files. httplib
    // buffers a multipart upload fully in memory before the route handler
    // runs (see MultipartFormData::get_file() below), so this is also an
    // upper bound on the RAM a single upload can consume -- generous enough
    // for realistic PGN files without allowing an unbounded one.
    server.set_payload_max_length(512 * 1024 * 1024);

    server.set_mount_point("/", webDir);
    server.set_file_extension_and_mimetype_mapping("js", "text/javascript; charset=utf-8");
    server.set_file_extension_and_mimetype_mapping("mjs", "text/javascript; charset=utf-8");
    server.set_file_extension_and_mimetype_mapping("svg", "image/svg+xml");
    server.set_default_headers({{"Cache-Control", "no-cache"}});

    // ---- "light" read routes: shared dbMutex lock, 503 if busy ---------

    server.Get("/api/info", [this](const httplib::Request&, httplib::Response& res) {
        std::shared_lock<std::shared_mutex> lock;
        if (!tryReadLock(lock)) { res.status = 503; res.set_content(busyJsonBody(jobManager->activeJob()), "application/json; charset=utf-8"); return; }
        res.set_content(apiInfoJson(), "application/json; charset=utf-8");
    });

    server.Get("/api/games", [this](const httplib::Request& req, httplib::Response& res) {
        std::shared_lock<std::shared_mutex> lock;
        if (!tryReadLock(lock)) { res.status = 503; res.set_content(busyJsonBody(jobManager->activeJob()), "application/json; charset=utf-8"); return; }
        std::map<std::string, std::string> params;
        for (int i = 0; GamesParamNames[i]; i++) {
            if (req.has_param(GamesParamNames[i])) {
                params[GamesParamNames[i]] = req.get_param_value(GamesParamNames[i]);
            }
        }
        res.set_content(apiGamesJson(params), "application/json; charset=utf-8");
    });

    server.Get("/api/game/:id", [this](const httplib::Request& req, httplib::Response& res) {
        std::shared_lock<std::shared_mutex> lock;
        if (!tryReadLock(lock)) { res.status = 503; res.set_content(busyJsonBody(jobManager->activeJob()), "application/json; charset=utf-8"); return; }
        auto id = std::atoi(req.path_params.at("id").c_str());
        bool found = false;
        auto body = apiGameDetailJson(id, found);
        if (!found) res.status = 404;
        res.set_content(body, "application/json; charset=utf-8");
    });

    server.Get("/api/pgn/:id", [this](const httplib::Request& req, httplib::Response& res) {
        std::shared_lock<std::shared_mutex> lock;
        if (!tryReadLock(lock)) { res.status = 503; res.set_content(busyJsonBody(jobManager->activeJob()), "text/plain; charset=utf-8"); return; }
        auto id = std::atoi(req.path_params.at("id").c_str());
        bool found = false;
        auto body = apiGamePgnText(id, found);
        if (!found) {
            res.status = 404;
            res.set_content("Game not found\n", "text/plain; charset=utf-8");
            return;
        }
        res.set_content(body, "text/plain; charset=utf-8");
    });

    server.Get("/api/players", [this](const httplib::Request& req, httplib::Response& res) {
        std::shared_lock<std::shared_mutex> lock;
        if (!tryReadLock(lock)) { res.status = 503; res.set_content(busyJsonBody(jobManager->activeJob()), "application/json; charset=utf-8"); return; }
        auto limit = req.has_param("limit") ? std::atoi(req.get_param_value("limit").c_str()) : 20;
        res.set_content(apiNameLookupJson("Players", req.get_param_value("q"), limit), "application/json; charset=utf-8");
    });
    server.Get("/api/events", [this](const httplib::Request& req, httplib::Response& res) {
        std::shared_lock<std::shared_mutex> lock;
        if (!tryReadLock(lock)) { res.status = 503; res.set_content(busyJsonBody(jobManager->activeJob()), "application/json; charset=utf-8"); return; }
        auto limit = req.has_param("limit") ? std::atoi(req.get_param_value("limit").c_str()) : 20;
        res.set_content(apiNameLookupJson("Events", req.get_param_value("q"), limit), "application/json; charset=utf-8");
    });
    server.Get("/api/sites", [this](const httplib::Request& req, httplib::Response& res) {
        std::shared_lock<std::shared_mutex> lock;
        if (!tryReadLock(lock)) { res.status = 503; res.set_content(busyJsonBody(jobManager->activeJob()), "application/json; charset=utf-8"); return; }
        auto limit = req.has_param("limit") ? std::atoi(req.get_param_value("limit").c_str()) : 20;
        res.set_content(apiNameLookupJson("Sites", req.get_param_value("q"), limit), "application/json; charset=utf-8");
    });

    server.Get("/api/stats", [this](const httplib::Request& req, httplib::Response& res) {
        std::shared_lock<std::shared_mutex> lock;
        if (!tryReadLock(lock)) { res.status = 503; res.set_content(busyJsonBody(jobManager->activeJob()), "application/json; charset=utf-8"); return; }
        res.set_content(apiStatsJson(req.has_param("refresh")), "application/json; charset=utf-8");
    });

    server.Get("/api/query", [this](const httplib::Request& req, httplib::Response& res) {
        std::shared_lock<std::shared_mutex> lock;
        if (!tryReadLock(lock)) { res.status = 503; res.set_content(busyJsonBody(jobManager->activeJob()), "application/json; charset=utf-8"); return; }
        auto limit = req.has_param("limit") ? std::atoi(req.get_param_value("limit").c_str()) : 50;
        res.set_content(apiQueryJson(req.get_param_value("pql"), limit), "application/json; charset=utf-8");
    });

    server.Get("/api/tree", [this](const httplib::Request& req, httplib::Response& res) {
        std::shared_lock<std::shared_mutex> lock;
        if (!tryReadLock(lock)) { res.status = 503; res.set_content(busyJsonBody(jobManager->activeJob()), "application/json; charset=utf-8"); return; }
        res.set_content(apiTreeJson(req.get_param_value("fen")), "application/json; charset=utf-8");
    });

    // ---- admin routes: token required, never mounted at "/" ------------

    server.Get("/api/admin/status", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkAdminAuth(req, res)) return;
        res.set_content(adminStatusJson(), "application/json; charset=utf-8");
    });

    server.Get("/api/admin/databases", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkAdminAuth(req, res)) return;
        res.set_content(adminDatabasesJson(), "application/json; charset=utf-8");
    });

    server.Post("/api/admin/databases/add", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkAdminAuth(req, res)) return;
        auto path = req.get_param_value("path");
        auto label = req.get_param_value("label");

        std::string err;
        if (path.empty()) {
            err = "path is required";
        } else if (!pathAllowed(path, err)) {
            // err already set
        } else if (!fs::exists(path)) {
            err = "path does not exist: " + path;
        } else {
            try {
                SQLite::Database check(path, SQLite::OPEN_READONLY);
                if (DbRead::getMoveField(&check) == SearchField::none) {
                    err = "not an OCGDB database (no Moves/Moves1/Moves2 field)";
                }
            } catch (SQLite::Exception& e) {
                err = std::string("could not open as SQLite database: ") + e.what();
            }
        }

        Json j; j.objBegin();
        if (!err.empty()) {
            res.status = 400;
            j.kv("error", err).objEnd();
        } else {
            auto entry = adminStore->addDatabase(path, label.empty() ? fs::path(path).filename().string() : label);
            j.kv("id", entry.id).kv("path", entry.path).objEnd();
        }
        res.set_content(j.str(), "application/json; charset=utf-8");
    });

    server.Post("/api/admin/databases/remove", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkAdminAuth(req, res)) return;
        auto id = std::atoi(req.get_param_value("id").c_str());
        auto ok = adminStore->removeDatabase(id);
        Json j; j.objBegin().kv("ok", ok).objEnd();
        res.set_content(j.str(), "application/json; charset=utf-8");
    });

    server.Post("/api/admin/databases/scan", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkAdminAuth(req, res)) return;
        auto dir = req.get_param_value("dir");

        std::string err;
        if (dir.empty() || !pathAllowed(dir, err)) {
            res.status = 400;
            Json j; j.objBegin().kv("error", err.empty() ? std::string("dir is required") : err).objEnd();
            res.set_content(j.str(), "application/json; charset=utf-8");
            return;
        }

        int added = 0, seen = 0;
        std::error_code ec;
        if (fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
            fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), endIt;
            for (; !ec && it != endIt; it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                auto name = it->path().filename().string();
                if (name.size() < 10 || name.compare(name.size() - 10, 10, ".ocgdb.db3") != 0) continue;
                seen++;
                try {
                    SQLite::Database check(it->path().string(), SQLite::OPEN_READONLY);
                    if (DbRead::getMoveField(&check) == SearchField::none) continue;
                } catch (SQLite::Exception&) { continue; }

                DbEntry existing;
                if (!adminStore->findDatabaseByPath(it->path().string(), existing)) added++;
                adminStore->addDatabase(it->path().string(), it->path().filename().string());
            }
        }
        Json j; j.objBegin().kv("added", added).kv("seen", seen).objEnd();
        res.set_content(j.str(), "application/json; charset=utf-8");
    });

    server.Post("/api/admin/databases/activate", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkAdminAuth(req, res)) return;
        auto id = std::atoi(req.get_param_value("id").c_str());
        DbEntry e;
        if (!adminStore->getDatabase(id, e)) {
            res.status = 404;
            Json j; j.objBegin().kv("error", "unknown database id").objEnd();
            res.set_content(j.str(), "application/json; charset=utf-8");
            return;
        }
        std::string err;
        auto ok = activateDatabase(e.path, err);
        Json j; j.objBegin().kv("ok", ok);
        if (!ok) j.kv("error", err);
        j.objEnd();
        res.set_content(j.str(), "application/json; charset=utf-8");
    });

    server.Get("/api/admin/jobs", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkAdminAuth(req, res)) return;
        res.set_content(adminJobsJson(req.get_param_value("state")), "application/json; charset=utf-8");
    });

    server.Get("/api/admin/jobs/:id", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkAdminAuth(req, res)) return;
        auto id = std::atoi(req.path_params.at("id").c_str());
        bool found = false;
        auto body = adminJobDetailJson(id, found);
        if (!found) res.status = 404;
        res.set_content(body, "application/json; charset=utf-8");
    });

    server.Get("/api/admin/jobs/:id/log", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkAdminAuth(req, res)) return;
        auto id = std::atoi(req.path_params.at("id").c_str());
        int64_t fromSeq = req.has_param("from") ? std::atoll(req.get_param_value("from").c_str()) : 0;
        res.set_content(adminJobLogJson(id, fromSeq), "application/json; charset=utf-8");
    });

    server.Post("/api/admin/jobs/submit", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkAdminAuth(req, res)) return;
        std::map<std::string, std::string> params;
        for (int i = 0; JobParamNames[i]; i++) {
            if (req.has_param(JobParamNames[i])) params[JobParamNames[i]] = req.get_param_value(JobParamNames[i]);
        }
        auto task = req.get_param_value("task");
        std::string err;
        auto id = jobManager->submit(task, params, err);
        Json j; j.objBegin();
        if (id < 0) {
            res.status = 400;
            j.kv("error", err).objEnd();
        } else {
            j.kv("id", id).objEnd();
        }
        res.set_content(j.str(), "application/json; charset=utf-8");
    });

    server.Post("/api/admin/jobs/:id/cancel", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkAdminAuth(req, res)) return;
        auto id = std::atoi(req.path_params.at("id").c_str());
        auto ok = jobManager->cancel(id);
        Json j; j.objBegin().kv("ok", ok).objEnd();
        res.set_content(j.str(), "application/json; charset=utf-8");
    });

    server.Post("/api/admin/jobs/clear", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkAdminAuth(req, res)) return;
        adminStore->clearFinishedJobs();
        Json j; j.objBegin().kv("ok", true).objEnd();
        res.set_content(j.str(), "application/json; charset=utf-8");
    });

    // Browser PGN upload: saves the multipart file to <root>/uploads (or,
    // with no -root, next to the admin db -- same fallback admin-token.txt
    // uses) and returns the server-side path, which the client then feeds
    // back in as the "pgn" param of a normal /api/admin/jobs/submit call.
    // Job submission itself still only ever accepts server-side paths (see
    // buildJobArgv(), jobs.cpp) -- this route's only job is turning "a file
    // the browser has open" into one of those.
    server.Post("/api/admin/upload", [this, adminDbPath](const httplib::Request& req, httplib::Response& res) {
        if (!checkAdminAuth(req, res)) return;
        namespace fs = std::filesystem;

        if (!req.form.has_file("file")) {
            res.status = 400;
            Json j; j.objBegin().kv("error", "missing 'file' part").objEnd();
            res.set_content(j.str(), "application/json; charset=utf-8");
            return;
        }
        auto file = req.form.get_file("file");
        if (file.content.empty()) {
            res.status = 400;
            Json j; j.objBegin().kv("error", "uploaded file is empty").objEnd();
            res.set_content(j.str(), "application/json; charset=utf-8");
            return;
        }

        // Keep only the basename -- some browsers/OSes send a full local
        // path as the filename, and this must never reach the destination
        // path unsanitized (directory traversal via "../../etc").
        auto baseName = fs::path(file.filename).filename().string();
        if (baseName.empty() || baseName == "." || baseName == "..") baseName = "upload.pgn";

        auto uploadDir = paraRecord.rootDir.empty()
            ? (fs::path(adminDbPath).parent_path() / "uploads")
            : (fs::path(paraRecord.rootDir) / "uploads");
        std::error_code ec;
        fs::create_directories(uploadDir, ec);

        // Random prefix so two uploads named the same never collide.
        auto destPath = (uploadDir / (randomHexToken(8) + "_" + baseName)).string();
        std::string perr;
        if (!pathAllowed(destPath, perr)) {
            res.status = 403;
            Json j; j.objBegin().kv("error", perr).objEnd();
            res.set_content(j.str(), "application/json; charset=utf-8");
            return;
        }

        std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            res.status = 500;
            Json j; j.objBegin().kv("error", "could not write uploaded file").objEnd();
            res.set_content(j.str(), "application/json; charset=utf-8");
            return;
        }
        out.write(file.content.data(), static_cast<std::streamsize>(file.content.size()));
        out.close();

        Json j; j.objBegin().kv("path", destPath).kv("size", static_cast<int64_t>(file.content.size())).objEnd();
        res.set_content(j.str(), "application/json; charset=utf-8");
    });

    // Streams a job's log lines + periodic status snapshots over one
    // held chunked HTTP connection, replacing the client's previous
    // separate polling of /jobs/:id (every 1.5s) and /jobs/:id/log (every
    // 1.2s) -- see web/js/admin.js. Not true event-driven push: AdminStore/
    // JobManager have no change-notification hook, so this polls
    // internally instead (immediately again while there's backlog to
    // drain, ~400ms sleep once caught up), just from inside one server-held
    // connection rather than many client-issued requests. EventSource is
    // deliberately NOT used to receive this: it cannot set the
    // X-OCGDB-Token header, the only auth this server has (checkAdminAuth
    // above) -- the client reads this via fetch() + ReadableStream instead.
    server.Get("/api/admin/jobs/:id/stream", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkAdminAuth(req, res)) return;
        auto jobId = std::atoi(req.path_params.at("id").c_str());

        JobEntry probeJob;
        if (!adminStore->getJob(jobId, probeJob)) {
            res.status = 404;
            Json j; j.objBegin().kv("error", "unknown job id").objEnd();
            res.set_content(j.str(), "application/json; charset=utf-8");
            return;
        }

        auto lastSeq = std::make_shared<int64_t>(req.has_param("from") ? std::atoll(req.get_param_value("from").c_str()) : 0);

        res.set_chunked_content_provider(
            "application/x-ndjson; charset=utf-8",
            [this, jobId, lastSeq](size_t /*offset*/, httplib::DataSink& sink) {
                auto lines = adminStore->getLog(jobId, *lastSeq);
                for (auto&& l : lines) {
                    Json j; j.objBegin().kv("type", "log").kv("seq", l.seq).kv("line", l.line).objEnd();
                    auto chunk = j.str() + "\n";
                    if (!sink.write(chunk.data(), chunk.size())) return false;
                    *lastSeq = l.seq;
                }

                JobEntry job;
                if (!adminStore->getJob(jobId, job)) { sink.done(); return false; }
                bool finished = job.state != "queued" && job.state != "running";

                Json sj; sj.objBegin().kv("type", "status").kv("state", job.state)
                    .kv("progress", job.progress).kv("progressTotal", job.progressTotal)
                    .kv("gameCnt", job.gameCnt).kv("exitCode", job.exitCode);
                if (!job.error.empty()) sj.kv("error", job.error);
                sj.kv("final", finished).objEnd();
                auto schunk = sj.str() + "\n";
                if (!sink.write(schunk.data(), schunk.size())) return false;

                if (finished) { sink.done(); return false; }
                if (!lines.empty()) return true; // more backlog may remain -- drain it now
                if (sink.is_writable && !sink.is_writable()) return false; // client gone
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
                return true;
            },
            [](bool /*success*/) {}
        );
    });

    std::cout << "Web UI folder: " << webDir << std::endl;
    std::cout << "OCGDB web UI:  http://127.0.0.1:" << paraRecord.port << "/" << std::endl;
    std::cout << "Admin token:   " << adminToken << std::endl;
    std::cout << "Admin URL:     http://127.0.0.1:" << paraRecord.port << "/#admin?token=" << adminToken << std::endl;
    std::cout << "(admin token also written to " << (fs::path(adminDbPath).parent_path() / "admin-token.txt").string() << ")" << std::endl;
    std::cout << "(listening on 127.0.0.1 only; press Ctrl+C to stop)" << std::endl;

    if (!server.listen("127.0.0.1", paraRecord.port)) {
        std::cerr << "Error: failed to start server on 127.0.0.1:" << paraRecord.port
                  << " (is the port already in use?)" << std::endl;
    }

    svr = nullptr;
}

////////////////////////////////////////////////////////////////////////////
// /api/info

std::string WebServer::apiInfoJson() const
{
    Json j;
    j.objBegin();

    j.key("info");
    j.objBegin();
    if (active.db) {
        SQLite::Statement stmt(*active.db, "SELECT Name, Value FROM Info");
        while (stmt.executeStep()) {
            j.kv(stmt.getColumn(0).getString().c_str(), stmt.getColumn(1).getString());
        }
    }
    j.objEnd();

    const char* moveFieldName = "";
    switch (active.searchField) {
        case SearchField::moves:  moveFieldName = "Moves"; break;
        case SearchField::moves1: moveFieldName = "Moves1"; break;
        case SearchField::moves2: moveFieldName = "Moves2"; break;
        default: break;
    }
    j.kv("moveField", moveFieldName);

    j.key("gamesColumns");
    j.arrBegin();
    for (auto&& c : active.gamesColumns) j.val(c);
    j.arrEnd();

    bool hasIdx = false;
    if (active.db) {
        SQLite::Statement stmt(*active.db,
            "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND tbl_name='Games' "
            "AND name NOT LIKE 'sqlite_autoindex%'");
        if (stmt.executeStep()) hasIdx = stmt.getColumn(0).getInt() > 0;
    }
    // Games has no secondary indexes in the OCGDB schema, so every filter/
    // sort/aggregate over it is a full table scan; the UI uses this to
    // warn on very large databases.
    j.kv("hasIndexes", hasIdx);

    // Whether -material has been run on this database -- see material.h.
    // PQL queries silently fall back to a full scan without it (never
    // wrong, just slower), so this is informational for the UI, not a
    // hard requirement anywhere.
    j.kv("hasGameMaterial", DbRead::hasTable(active.db, "GameMaterial"));

    // Whether -tree has been run -- see tree.h; the UI uses this to decide
    // whether to show opening-tree stats next to the board.
    j.kv("hasOpeningTree", DbRead::hasTable(active.db, "OpeningTree"));

    // Whether -o parseeval was used and -o index has resolved ECO names --
    // see builder.cpp/addgame.cpp and getAllEcoNames() (chess.cpp).
    j.kv("hasEvals", DbRead::hasTable(active.db, "Evals"));
    j.kv("hasOpenings", DbRead::hasTable(active.db, "Openings"));

    j.kv("dbPath", active.path);

    j.objEnd();
    return j.str();
}

////////////////////////////////////////////////////////////////////////////
// /api/games

std::string WebServer::apiGamesJson(const std::map<std::string, std::string>& params) const
{
    auto getP = [&](const char* k) -> std::string {
        auto it = params.find(k);
        return it == params.end() ? std::string() : it->second;
    };

    int offset = std::max(0, std::atoi(getP("offset").c_str()));
    int limit = getP("limit").empty() ? 50 : std::atoi(getP("limit").c_str());
    if (limit <= 0) limit = 50;
    if (limit > 500) limit = 500;

    // kind 0 = text bind, 1 = integer bind
    std::vector<std::pair<int, std::string>> binds;
    std::vector<std::string> conds;

    auto addLike = [&](const char* col, const std::string& val) {
        if (val.empty()) return;
        conds.push_back(std::string(col) + " LIKE ?");
        binds.push_back({0, "%" + val + "%"});
    };
    auto addPrefix = [&](const char* col, const std::string& val) {
        if (val.empty()) return;
        conds.push_back(std::string(col) + " LIKE ?");
        binds.push_back({0, val + "%"});
    };
    auto addEq = [&](const char* col, const std::string& val) {
        if (val.empty()) return;
        conds.push_back(std::string(col) + " = ?");
        binds.push_back({0, val});
    };
    auto addIntCmp = [&](const char* col, const char* op, const std::string& val) {
        if (val.empty()) return;
        conds.push_back(std::string(col) + " " + op + " ?");
        binds.push_back({1, val});
    };

    addLike("w.Name", getP("white"));
    addLike("b.Name", getP("black"));
    if (!getP("player").empty()) {
        conds.push_back("(w.Name LIKE ? OR b.Name LIKE ?)");
        auto v = "%" + getP("player") + "%";
        binds.push_back({0, v});
        binds.push_back({0, v});
    }
    addLike("e.Name", getP("event"));
    addLike("s.Name", getP("site"));

    if (hasGamesColumn("Result")) addEq("g.Result", getP("result"));
    if (hasGamesColumn("ECO")) addPrefix("g.ECO", getP("eco"));

    if (hasGamesColumn("WhiteElo") && hasGamesColumn("BlackElo")) {
        if (!getP("minElo").empty()) {
            conds.push_back("(g.WhiteElo >= ? OR g.BlackElo >= ?)");
            binds.push_back({1, getP("minElo")});
            binds.push_back({1, getP("minElo")});
        }
        if (!getP("maxElo").empty()) {
            conds.push_back("(g.WhiteElo <= ? AND g.BlackElo <= ?)");
            binds.push_back({1, getP("maxElo")});
            binds.push_back({1, getP("maxElo")});
        }
    }
    if (hasGamesColumn("Date")) {
        addIntCmp("g.Date", ">=", getP("dateFrom"));
        addIntCmp("g.Date", "<=", getP("dateTo"));
    }
    if (hasGamesColumn("PlyCount")) {
        addIntCmp("g.PlyCount", ">=", getP("minPly"));
    }

    std::string sortCol = "g.ID";
    auto sortKey = getP("sort");
    if (sortKey == "date" && hasGamesColumn("Date")) sortCol = "g.Date";
    else if (sortKey == "elo" && hasGamesColumn("WhiteElo")) sortCol = "g.WhiteElo";
    else if (sortKey == "plycount" && hasGamesColumn("PlyCount")) sortCol = "g.PlyCount";
    std::string dir = (getP("dir") == "desc") ? "DESC" : "ASC";

    std::string whereClause;
    if (!conds.empty()) {
        whereClause = " WHERE ";
        for (size_t i = 0; i < conds.size(); i++) {
            if (i) whereClause += " AND ";
            whereClause += conds[i];
        }
    }

    static const std::string gamesFromJoin =
        "FROM Games g "
        "INNER JOIN Players w ON WhiteID = w.ID "
        "INNER JOIN Players b ON BlackID = b.ID "
        "INNER JOIN Events e ON EventID = e.ID "
        "INNER JOIN Sites s ON SiteID = s.ID";

    bool filtered = !conds.empty();
    int64_t total = 0;
    bool exactTotal = filtered;

    if (!filtered && active.db) {
        // Info.GameCount is the project's own authoritative cache (kept in
        // sync by Builder/AddGame) precisely so callers never need a slow
        // COUNT(*) -- treat it as exact, matching how the rest of the
        // codebase (e.g. DbCore::queryInfo) already trusts it.
        SQLite::Statement stmt(*active.db, "SELECT Value FROM Info WHERE Name = 'GameCount'");
        if (stmt.executeStep()) {
            total = stmt.getColumn(0).getInt64();
        } else {
            SQLite::Statement c(*active.db, "SELECT COUNT(*) FROM Games");
            if (c.executeStep()) total = c.getColumn(0).getInt64();
        }
        exactTotal = true;
    } else if (filtered && active.db) {
        SQLite::Statement cstmt(*active.db, "SELECT COUNT(*) " + gamesFromJoin + whereClause);
        int idx = 1;
        for (auto&& b : binds) {
            if (b.first == 0) cstmt.bind(idx++, b.second);
            else cstmt.bind(idx++, static_cast<long long>(std::atoll(b.second.c_str())));
        }
        if (cstmt.executeStep()) total = cstmt.getColumn(0).getInt64();
    }

    Json j;
    j.objBegin();
    j.kv("total", total);
    j.kv("offset", offset);
    j.kv("limit", limit);
    j.kv("exactTotal", exactTotal);

    j.key("games");
    j.arrBegin();

    if (active.db) {
        bool hasDate = hasGamesColumn("Date");
        bool hasRound = hasGamesColumn("Round");
        bool hasWhiteElo = hasGamesColumn("WhiteElo");
        bool hasBlackElo = hasGamesColumn("BlackElo");
        bool hasResult = hasGamesColumn("Result");
        bool hasTimeControl = hasGamesColumn("TimeControl");
        bool hasEco = hasGamesColumn("ECO");
        bool hasPlyCount = hasGamesColumn("PlyCount");
        bool hasFen = hasGamesColumn("FEN");

        std::string sql = DbRead::fullGameQueryString + whereClause +
            " ORDER BY " + sortCol + " " + dir + " LIMIT ? OFFSET ?";
        SQLite::Statement stmt(*active.db, sql);
        int idx = 1;
        for (auto&& b : binds) {
            if (b.first == 0) stmt.bind(idx++, b.second);
            else stmt.bind(idx++, static_cast<long long>(std::atoll(b.second.c_str())));
        }
        stmt.bind(idx++, limit);
        stmt.bind(idx++, offset);

        while (stmt.executeStep()) {
            j.objBegin();
            j.kv("id", stmt.getColumn("ID").getInt());
            j.kv("event", stmt.getColumn("Event").getString());
            j.kv("site", stmt.getColumn("Site").getString());
            j.kv("white", stmt.getColumn("White").getString());
            j.kv("black", stmt.getColumn("Black").getString());
            if (hasDate) j.kv("date", stmt.getColumn("Date")); else j.kvNull("date");
            if (hasRound) j.kv("round", stmt.getColumn("Round")); else j.kvNull("round");
            if (hasWhiteElo) j.kv("whiteElo", stmt.getColumn("WhiteElo")); else j.kvNull("whiteElo");
            if (hasBlackElo) j.kv("blackElo", stmt.getColumn("BlackElo")); else j.kvNull("blackElo");
            if (hasResult) j.kv("result", stmt.getColumn("Result")); else j.kvNull("result");
            if (hasTimeControl) j.kv("timeControl", stmt.getColumn("TimeControl")); else j.kvNull("timeControl");
            if (hasEco) j.kv("eco", stmt.getColumn("ECO")); else j.kvNull("eco");
            if (hasPlyCount) j.kv("plyCount", stmt.getColumn("PlyCount")); else j.kvNull("plyCount");
            if (hasFen) {
                auto fen = stmt.getColumn("FEN").getString();
                j.kv("hasFen", !fen.empty());
            } else {
                j.kv("hasFen", false);
            }
            j.objEnd();
        }
    }

    j.arrEnd();
    j.objEnd();
    return j.str();
}

////////////////////////////////////////////////////////////////////////////
// /api/game/:id and /api/pgn/:id

bool WebServer::loadGameDetail(int gameID, bslib::PgnRecord& record, bslib::BoardCore* board) const
{
    if (!active.db || gameID <= 0) return false;

    SQLite::Statement stmt(*active.db, DbRead::fullGameQueryString + " WHERE g.ID = ?");
    stmt.bind(1, gameID);
    if (!stmt.executeStep()) return false;

    record.gameID = gameID;
    DbRead::extractHeader(stmt, record);

    board->newGame(record.fenText);

    if (active.searchField == SearchField::moves1 || active.searchField == SearchField::moves2) {
        auto moveName = (active.searchField == SearchField::moves1) ? "Moves1" : "Moves2";

        int flag = bslib::BoardCore::ParseMoveListFlag_create_san
                  | bslib::BoardCore::ParseMoveListFlag_create_fen;
        if (active.searchField == SearchField::moves1) {
            flag |= bslib::BoardCore::ParseMoveListFlag_move_size_1_byte;
        }

        auto c = stmt.getColumn(moveName);
        auto moveBlob = static_cast<const int8_t*>(c.getBlob());
        if (moveBlob) {
            std::vector<int8_t> moveVec(moveBlob, moveBlob + c.size());
            board->fromMoveList(&record, moveVec, flag, nullptr);
        }
    } else if (active.searchField == SearchField::moves) {
        record.moveString = stmt.getColumn("Moves").getString();

        int flag = bslib::BoardCore::ParseMoveListFlag_quick_check
                  | bslib::BoardCore::ParseMoveListFlag_create_san
                  | bslib::BoardCore::ParseMoveListFlag_create_fen;
        board->fromMoveList(&record, bslib::Notation::san, flag, nullptr);
    }

    SQLite::Statement cstmt(*active.db, "SELECT * FROM Comments WHERE GameID = ?");
    cstmt.bind(1, gameID);
    while (cstmt.executeStep()) {
        auto comment = cstmt.getColumn("Comment").getString();
        if (comment.empty()) continue;

        auto ply = cstmt.getColumn("Ply").getInt();
        if (ply >= 0 && ply < board->getHistListSize()) {
            board->_getHistPointerAt(ply)->comment = comment;
        } else {
            board->setFirstComment(comment);
        }
    }

    return true;
}

std::string WebServer::apiGameDetailJson(int id, bool& found) const
{
    bslib::PgnRecord record;
    std::unique_ptr<bslib::BoardCore> board(bslib::Funcs::createBoard(bslib::ChessVariant::standard));

    found = loadGameDetail(id, record, board.get());

    Json j;
    j.objBegin();

    if (!found) {
        j.kv("error", "game not found");
        j.objEnd();
        return j.str();
    }

    j.kv("id", id);

    j.key("tags");
    j.objBegin();
    for (auto&& it : record.tags) {
        j.kv(it.first.c_str(), it.second);
    }
    j.objEnd();

    // Games.ECO only ever stored the bare 3-character code (see
    // Builder::createDb()'s ECO handling, builder.cpp); Openings (built
    // from the same static reference table every -create run populates
    // it from, see chess.cpp's getAllEcoNames()) resolves it to a real
    // name when available.
    {
        auto ecoIt = record.tags.find("ECO");
        if (ecoIt != record.tags.end() && !ecoIt->second.empty() &&
            active.db && DbRead::hasTable(active.db, "Openings")) {
            SQLite::Statement stmt(*active.db, "SELECT Name FROM Openings WHERE ECO = ?");
            stmt.bind(1, ecoIt->second);
            if (stmt.executeStep()) {
                j.kv("openingName", stmt.getColumn(0).getString());
            }
        }
    }

    j.kv("startFen", record.fenText);
    j.kv("firstComment", board->getFirstComment());

    // Ply -> engine eval, if this database has one (-o parseeval, see
    // builder.cpp/addgame.cpp); attached to each move below.
    std::unordered_map<int, std::tuple<int64_t, int64_t, int64_t>> evalByPly; // ply -> (depth, score, mate)
    if (active.db && DbRead::hasTable(active.db, "Evals")) {
        SQLite::Statement stmt(*active.db, "SELECT Ply, Depth, Score, Mate FROM Evals WHERE GameID = ?");
        stmt.bind(1, id);
        while (stmt.executeStep()) {
            evalByPly[stmt.getColumn("Ply").getInt()] = {
                stmt.getColumn("Depth").getInt64(), stmt.getColumn("Score").getInt64(), stmt.getColumn("Mate").getInt64()
            };
        }
    }

    auto histList = board->getHistList();

    j.key("moves");
    j.arrBegin();
    for (size_t i = 0; i < histList.size(); i++) {
        auto&& h = histList[i];
        j.objBegin();
        j.kv("ply", static_cast<int64_t>(i));
        j.kv("no", static_cast<int64_t>(i / 2 + 1));
        j.kv("side", h.move.piece.side == bslib::Side::white ? "w" : "b");
        j.kv("san", h.sanString);

        // from/dest are bslib::ChessPos indexes (a8=0 .. h1=63), NOT the
        // common a1=0 convention. The client should never need the raw
        // index -- "uci" (e.g. "e2e4", "e7e8q") is always included and is
        // what board.js actually uses.
        std::string uci;
        if (h.move.isValid()) {
            uci = board->posToCoordinateString(h.move.from) + board->posToCoordinateString(h.move.dest);
            if (h.move.promotion > bslib::KING) {
                auto pc = board->pieceType2Char(h.move.promotion);
                uci += static_cast<char>(std::tolower(static_cast<unsigned char>(pc)));
            }
        }
        j.kv("uci", uci);
        j.kv("from", h.move.from);
        j.kv("to", h.move.dest);
        j.kv("fen", h.fenString);
        j.kv("comment", h.comment);

        auto evalIt = evalByPly.find(static_cast<int>(i));
        if (evalIt != evalByPly.end()) {
            j.key("eval");
            j.objBegin();
            j.kv("depth", std::get<0>(evalIt->second));
            j.kv("score", std::get<1>(evalIt->second));
            j.kv("mate", std::get<2>(evalIt->second));
            j.objEnd();
        } else {
            j.kvNull("eval");
        }

        j.objEnd();
    }
    j.arrEnd();

    j.kv("finalFen", board->getFen());
    j.kv("pgn", board->toPgn(&record));

    j.objEnd();
    return j.str();
}

std::string WebServer::apiGamePgnText(int id, bool& found) const
{
    bslib::PgnRecord record;
    std::unique_ptr<bslib::BoardCore> board(bslib::Funcs::createBoard(bslib::ChessVariant::standard));

    found = loadGameDetail(id, record, board.get());
    if (!found) return std::string();

    return board->toPgn(&record);
}

////////////////////////////////////////////////////////////////////////////
// /api/players, /api/events, /api/sites
//
// `table` is only ever one of the three string literals passed by the
// route lambdas in runTask() above -- never derived from the request -- so
// concatenating it into the SQL text here is safe.

std::string WebServer::apiNameLookupJson(const char* table, const std::string& q, int limit) const
{
    if (limit <= 0) limit = 20;
    if (limit > 200) limit = 200;

    bool isPlayers = std::string(table) == "Players";

    Json j;
    j.objBegin();
    j.key("items");
    j.arrBegin();

    if (active.db) {
        std::string sql = std::string("SELECT ID, Name") + (isPlayers ? ", Elo" : "") +
            " FROM " + table + " WHERE Name LIKE ? ORDER BY Name LIMIT ?";
        SQLite::Statement stmt(*active.db, sql);
        stmt.bind(1, q + "%");
        stmt.bind(2, limit);
        while (stmt.executeStep()) {
            j.objBegin();
            j.kv("id", stmt.getColumn("ID").getInt());
            j.kv("name", stmt.getColumn("Name").getString());
            if (isPlayers) j.kv("elo", stmt.getColumn("Elo"));
            j.objEnd();
        }
    }

    j.arrEnd();
    j.objEnd();
    return j.str();
}

////////////////////////////////////////////////////////////////////////////
// /api/stats

std::string WebServer::apiStatsJson(bool refresh)
{
    std::lock_guard<std::mutex> lk(statsMutex);
    if (!refresh && !active.statsCache.empty()) return active.statsCache;

    Json j;
    j.objBegin();

    if (active.db) {
        j.key("results");
        j.arrBegin();
        if (hasGamesColumn("Result")) {
            SQLite::Statement stmt(*active.db, "SELECT Result, COUNT(*) c FROM Games GROUP BY Result ORDER BY c DESC");
            while (stmt.executeStep()) {
                j.objBegin();
                j.kv("result", stmt.getColumn(0).getString());
                j.kv("count", stmt.getColumn(1).getInt64());
                j.objEnd();
            }
        }
        j.arrEnd();

        j.key("perYear");
        j.arrBegin();
        if (hasGamesColumn("Date")) {
            SQLite::Statement stmt(*active.db,
                "SELECT substr(Date,1,4) y, COUNT(*) c FROM Games "
                "WHERE Date IS NOT NULL AND length(Date) >= 4 GROUP BY y ORDER BY y");
            while (stmt.executeStep()) {
                j.objBegin();
                j.kv("year", stmt.getColumn(0).getString());
                j.kv("count", stmt.getColumn(1).getInt64());
                j.objEnd();
            }
        }
        j.arrEnd();

        j.key("topEco");
        j.arrBegin();
        if (hasGamesColumn("ECO")) {
            SQLite::Statement stmt(*active.db,
                "SELECT ECO, COUNT(*) c FROM Games WHERE ECO IS NOT NULL AND ECO <> '' "
                "GROUP BY ECO ORDER BY c DESC LIMIT 15");
            while (stmt.executeStep()) {
                j.objBegin();
                j.kv("eco", stmt.getColumn(0).getString());
                j.kv("count", stmt.getColumn(1).getInt64());
                j.objEnd();
            }
        }
        j.arrEnd();

        j.key("eloBuckets");
        j.arrBegin();
        if (hasGamesColumn("WhiteElo") && hasGamesColumn("BlackElo")) {
            SQLite::Statement stmt(*active.db,
                "SELECT (elo/100)*100 bucket, COUNT(*) c FROM ("
                "  SELECT WhiteElo elo FROM Games WHERE WhiteElo > 0"
                "  UNION ALL SELECT BlackElo FROM Games WHERE BlackElo > 0"
                ") GROUP BY bucket ORDER BY bucket");
            while (stmt.executeStep()) {
                j.objBegin();
                j.kv("bucket", stmt.getColumn(0).getInt64());
                j.kv("count", stmt.getColumn(1).getInt64());
                j.objEnd();
            }
        }
        j.arrEnd();

        j.key("plyBuckets");
        j.arrBegin();
        if (hasGamesColumn("PlyCount")) {
            SQLite::Statement stmt(*active.db,
                "SELECT (PlyCount/10)*10 bucket, COUNT(*) c FROM Games WHERE PlyCount > 0 "
                "GROUP BY bucket ORDER BY bucket");
            while (stmt.executeStep()) {
                j.objBegin();
                j.kv("bucket", stmt.getColumn(0).getInt64());
                j.kv("count", stmt.getColumn(1).getInt64());
                j.objEnd();
            }
        }
        j.arrEnd();
    }

    j.objEnd();
    active.statsCache = j.str();
    return active.statsCache;
}

////////////////////////////////////////////////////////////////////////////
// /api/query (PQL) -- see also processAGameWithAThread() below

std::string WebServer::apiQueryJson(const std::string& pql, int limit)
{
    if (limit <= 0) limit = 50;
    if (limit > 2000) limit = 2000;

    std::lock_guard<std::mutex> lk(pqlMutex);

    Json j;
    j.objBegin();

    // Strip "// ..." comments the same way the CLI path (Search::runTask(),
    // search.cpp) does -- previously only the CLI stripped them, so a query
    // with a comment that worked from the command line failed here.
    auto q = Parser::stripLineComments(pql);
    bslib::Funcs::trim(q);
    if (q.empty()) {
        j.kv("ok", false);
        j.kv("error", "empty query");
        j.objEnd();
        return j.str();
    }

    if (!parser.parse(chessVariant, q.c_str())) {
        j.kv("ok", false);
        j.kv("error", parser.getErrorString());
        j.objEnd();
        return j.str();
    }

    {
        std::lock_guard<std::mutex> lk2(pqlHitsMutex);
        pqlHits.clear();
    }
    pqlLimit = static_cast<size_t>(limit);
    succCount = 0;
    errCnt = 0;

    // `limit` only bounds how many matching games are RETURNED (pqlLimit,
    // above) -- it must not also bound how many games are SCANNED. Doing
    // both with the same number (an earlier version of this code did) made
    // scanned/matched non-deterministic: DbRead::readADb() dispatches games
    // to the shared thread pool asynchronously and only checks `succCount
    // >= resultNumberLimit` once per dispatched row, so by the time enough
    // matches have actually finished processing, many more rows than
    // `limit` are typically already queued or mid-flight, and exactly how
    // many varies run to run. Default to a full, deterministic scan (every
    // game, exact counts, reproducible) unless the database is large enough
    // that a full scan would be a poor fit for a synchronous HTTP request,
    // in which case fall back to a generous (not display-sized) cap and
    // let `truncated` tell the UI the numbers are a lower bound.
    int64_t totalGames = 0;
    if (active.db) {
        SQLite::Statement stmt(*active.db, "SELECT Value FROM Info WHERE Name = 'GameCount'");
        if (stmt.executeStep()) totalGames = stmt.getColumn(0).getInt64();
    }
    const int64_t kFullScanThreshold = 300000; // ~10-15s at the -bench throughput of ~20-30k games/s
    if (totalGames > 0 && totalGames <= kFullScanThreshold) {
        paraRecord.resultNumberLimit = 0xffffffffffffULL;
    } else {
        paraRecord.resultNumberLimit = std::max<int64_t>(20000, static_cast<int64_t>(limit) * 50);
    }

    // Metadata terms (whiteplayer/welo/date/eco/...) need White/Black/
    // Event/Site resolved by name -- only fullGameQueryString joins for
    // that; the plain "SELECT * FROM Games" only has the raw *ID columns.
    auto baseQueryString = parser.metadataNeedsNames() ? DbRead::fullGameQueryString : std::string("SELECT * FROM Games");
    auto queryString = baseQueryString;

    std::vector<std::string> whereClauses;
    auto metadataWhere = parser.getMetadataWhereSql();
    if (!metadataWhere.empty()) whereClauses.push_back(metadataWhere);
    auto materialFilter = parser.buildMaterialPreFilterSql();
    if (!materialFilter.empty() && DbRead::hasTable(active.db, "GameMaterial")) {
        whereClauses.push_back("ID IN (SELECT GameID FROM GameMaterial WHERE " + materialFilter + ")");
    }
    if (!whereClauses.empty()) {
        std::string combined;
        for (auto&& w : whereClauses) {
            if (!combined.empty()) combined += " AND ";
            combined += "(" + w + ")";
        }
        queryString = "SELECT * FROM (" + baseQueryString + ") WHERE " + combined;
    }

    auto t0 = std::chrono::steady_clock::now();
    readADb(active.path, queryString);
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    std::vector<int> hits;
    {
        std::lock_guard<std::mutex> lk2(pqlHitsMutex);
        hits = pqlHits;
    }

    j.kv("ok", true);
    j.kv("scanned", static_cast<int64_t>(gameCnt));
    j.kv("matched", static_cast<int64_t>(succCount));
    j.kv("truncated", static_cast<int64_t>(succCount) > static_cast<int64_t>(hits.size()));
    j.kv("elapsedMs", static_cast<int64_t>(elapsedMs));

    j.key("games");
    j.arrBegin();
    if (active.db) {
        for (auto gid : hits) {
            SQLite::Statement stmt(*active.db, DbRead::fullGameQueryString + " WHERE g.ID = ?");
            stmt.bind(1, gid);
            if (!stmt.executeStep()) continue;

            j.objBegin();
            j.kv("id", gid);
            j.kv("event", stmt.getColumn("Event").getString());
            j.kv("white", stmt.getColumn("White").getString());
            j.kv("black", stmt.getColumn("Black").getString());
            if (hasGamesColumn("Result")) j.kv("result", stmt.getColumn("Result")); else j.kvNull("result");
            if (hasGamesColumn("Date")) j.kv("date", stmt.getColumn("Date")); else j.kvNull("date");
            j.objEnd();
        }
    }
    j.arrEnd();

    j.objEnd();
    return j.str();
}

////////////////////////////////////////////////////////////////////////////
// /api/tree

std::string WebServer::apiTreeJson(const std::string& fen) const
{
    Json j;
    j.objBegin();

    if (!active.db || !DbRead::hasTable(active.db, "OpeningTree")) {
        j.kv("ok", false);
        j.kv("error", "this database has no opening tree yet (run the tree task first)");
        j.objEnd();
        return j.str();
    }

    // fen empty -> BoardCore::newGame()'s own default is the standard
    // starting position (every task that reads record.fenText, which is
    // empty for the overwhelming majority of games, already relies on
    // this exact behavior).
    auto board = bslib::Funcs::createBoard(bslib::ChessVariant::standard);
    board->newGame(fen);
    // hashKey is uint64_t; OpeningTree stores it as a plain SQLite INTEGER
    // (signed 64-bit) the same way Hist::hashKey (int64_t) does when
    // -tree builds the table (tree.cpp) -- a bit-pattern-preserving
    // reinterpretation, consistent both ways.
    auto hashKey = static_cast<int64_t>(board->hashKey);
    delete board;

    j.kv("ok", true);
    j.key("moves");
    j.arrBegin();
    try {
        SQLite::Statement stmt(*active.db,
            "SELECT San, Games, WhiteWin, Draw, BlackWin, EloSum, EloCnt, LastYear "
            "FROM OpeningTree WHERE HashKey = ? ORDER BY Games DESC");
        stmt.bind(1, hashKey);
        while (stmt.executeStep()) {
            j.objBegin();
            j.kv("san", stmt.getColumn("San").getString());
            j.kv("games", stmt.getColumn("Games").getInt64());
            j.kv("whiteWin", stmt.getColumn("WhiteWin").getInt64());
            j.kv("draw", stmt.getColumn("Draw").getInt64());
            j.kv("blackWin", stmt.getColumn("BlackWin").getInt64());
            auto eloCnt = stmt.getColumn("EloCnt").getInt64();
            if (eloCnt > 0) {
                j.kv("avgElo", stmt.getColumn("EloSum").getInt64() / eloCnt);
            } else {
                j.kvNull("avgElo");
            }
            j.kv("lastYear", stmt.getColumn("LastYear").getInt64());
            j.objEnd();
        }
    } catch (std::exception& e) {
        j.kv("ok", false);
        j.kv("error", std::string("could not query OpeningTree: ") + e.what());
    }
    j.arrEnd();

    j.objEnd();
    return j.str();
}

void WebServer::processAGameWithAThread(ThreadRecord* t, const bslib::PgnRecord& record, const std::vector<int8_t>& moveVec)
{
    assert(t);

    if (!t->board) {
        t->board = bslib::Funcs::createBoard(bslib::ChessVariant::standard);
    }
    assert(t->board);

    t->board->newGame(record.fenText);

    int flag = bslib::BoardCore::ParseMoveListFlag_create_bitboard;
    if (searchField == SearchField::moves) {
        flag |= bslib::BoardCore::ParseMoveListFlag_quick_check;
        t->board->fromMoveList(&record, bslib::Notation::san, flag, nullptr);
    } else {
        if (searchField == SearchField::moves1) {
            flag |= bslib::BoardCore::ParseMoveListFlag_move_size_1_byte;
        }
        t->board->fromMoveList(&record, moveVec, flag, nullptr);
    }

    t->gameCnt++;

    // See the identical loop (and comment) in Search::runTask() (search.cpp):
    // histList[i] is the position right after move i+1, so every position in
    // the game is i = 0 .. n-1; the previous i = 1 .. n bound silently
    // skipped histList[0] (the position right after White's first move).
    for (int i = 0, n = t->board->getHistListSize(); i < n; i++) {
        std::vector<uint64_t> bitboardVec;
        if (i < n - 1) {
            auto hist = t->board->_getHistPointerAt(i);
            if (!hist || hist->bitboardVec.empty()) continue;
            bitboardVec = hist->bitboardVec;
        } else {
            bitboardVec = t->board->posToBitboards();
        }

        if (!parser.evaluate(bitboardVec)) continue;

        std::lock_guard<std::mutex> lk(pqlHitsMutex);
        succCount++;
        if (pqlHits.size() < pqlLimit) {
            pqlHits.push_back(record.gameID);
        }
        break;
    }
}

////////////////////////////////////////////////////////////////////////////
// /api/admin/status

std::string WebServer::adminStatusJson() const
{
    Json j;
    j.objBegin();

    j.kv("version", VersionString);
    j.kv("pid", static_cast<int64_t>(
#ifdef _WIN32
        GetCurrentProcessId()
#else
        getpid()
#endif
    ));

    auto uptimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(getNow() - startTime).count();
    j.kv("uptimeMs", static_cast<int64_t>(uptimeMs));
    j.kv("port", paraRecord.port);
    j.kv("cpu", static_cast<int64_t>(std::thread::hardware_concurrency()));
    j.kv("rootDir", paraRecord.rootDir);

    {
        std::shared_lock<std::shared_mutex> lock(dbMutex);
        j.key("activeDb");
        j.objBegin();
        j.kv("path", active.path);
        j.kv("open", active.db != nullptr);
        j.objEnd();
    }

    int activeJobId = jobManager ? jobManager->activeJob() : -1;
    j.kv("busy", activeJobId >= 0);
    if (activeJobId >= 0) j.kv("activeJobId", activeJobId); else j.kvNull("activeJobId");

    j.key("jobCounts");
    j.objBegin();
    if (adminStore) {
        auto all = adminStore->listJobs("");
        int64_t queued = 0, running = 0, succeeded = 0, failed = 0, cancelled = 0;
        for (auto&& e : all) {
            if (e.state == "queued") queued++;
            else if (e.state == "running") running++;
            else if (e.state == "succeeded") succeeded++;
            else if (e.state == "failed") failed++;
            else if (e.state == "cancelled") cancelled++;
        }
        j.kv("queued", queued);
        j.kv("running", running);
        j.kv("succeeded", succeeded);
        j.kv("failed", failed);
        j.kv("cancelled", cancelled);
    }
    j.objEnd();

    j.objEnd();
    return j.str();
}

////////////////////////////////////////////////////////////////////////////
// /api/admin/databases

std::string WebServer::adminDatabasesJson() const
{
    namespace fs = std::filesystem;

    std::string activePath;
    {
        std::shared_lock<std::shared_mutex> lock(dbMutex);
        activePath = active.path;
    }

    Json j;
    j.objBegin();
    j.key("databases");
    j.arrBegin();

    if (adminStore) {
        for (auto&& e : adminStore->listDatabases()) {
            j.objBegin();
            j.kv("id", e.id);
            j.kv("path", e.path);
            j.kv("label", e.label);
            j.kv("addedAt", e.addedAt);
            j.kv("lastOpenedAt", e.lastOpenedAt);
            j.kv("note", e.note);
            j.kv("isActive", e.path == activePath);

            std::error_code ec;
            auto exists = fs::exists(e.path, ec) && !ec;
            j.kv("exists", exists);
            j.kv("sizeBytes", exists ? static_cast<int64_t>(fs::file_size(e.path, ec)) : static_cast<int64_t>(0));

            int64_t gameCount = -1;
            std::string moveField;
            if (exists) {
                try {
                    SQLite::Database check(e.path, SQLite::OPEN_READONLY);
                    auto sf = DbRead::getMoveField(&check);
                    moveField = (sf == SearchField::moves) ? "Moves" : (sf == SearchField::moves1) ? "Moves1"
                              : (sf == SearchField::moves2) ? "Moves2" : "";
                    SQLite::Statement stmt(check, "SELECT Value FROM Info WHERE Name = 'GameCount'");
                    if (stmt.executeStep()) gameCount = stmt.getColumn(0).getInt64();
                } catch (SQLite::Exception&) {
                    // leave gameCount = -1 / moveField empty; UI treats this as "unreadable"
                }
            }
            j.kv("gameCount", gameCount);
            j.kv("moveField", moveField);

            j.objEnd();
        }
    }

    j.arrEnd();
    j.objEnd();
    return j.str();
}

////////////////////////////////////////////////////////////////////////////
// /api/admin/jobs*

std::string WebServer::adminJobsJson(const std::string& stateFilter) const
{
    Json j;
    j.objBegin();
    j.key("jobs");
    j.arrBegin();
    if (adminStore) {
        for (auto&& e : adminStore->listJobs(stateFilter)) writeJobEntry(j, e);
    }
    j.arrEnd();
    j.objEnd();
    return j.str();
}

std::string WebServer::adminJobDetailJson(int id, bool& found) const
{
    JobEntry e;
    found = adminStore && adminStore->getJob(id, e);

    Json j;
    j.objBegin();
    if (!found) {
        j.kv("error", "job not found");
        j.objEnd();
        return j.str();
    }
    j.key("job");
    writeJobEntry(j, e);
    j.objEnd();
    return j.str();
}

std::string WebServer::adminJobLogJson(int id, int64_t fromSeq) const
{
    Json j;
    j.objBegin();
    j.key("lines");
    j.arrBegin();
    int64_t nextSeq = fromSeq;
    if (adminStore) {
        for (auto&& l : adminStore->getLog(id, fromSeq)) {
            j.objBegin();
            j.kv("seq", l.seq);
            j.kv("line", l.line);
            j.objEnd();
            nextSeq = l.seq;
        }
    }
    j.arrEnd();
    j.kv("nextSeq", nextSeq);
    j.objEnd();
    return j.str();
}
