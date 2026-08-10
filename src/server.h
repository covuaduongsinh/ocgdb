/**
 * This file is part of Open Chess Game Database Standard.
 *
 * WebServer implements the `-server` task: a local HTTP server exposing a
 * JSON API (used by the web UI in web/) plus static file hosting for that
 * UI. See src/server.cpp for the endpoint implementations.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#ifndef OCGDB_SERVER_H
#define OCGDB_SERVER_H

#include <mutex>
#include <shared_mutex>
#include <vector>
#include <string>
#include <map>

#include "dbread.h"
#include "parser.h"
#include "admin.h"
#include "jobs.h"

// httplib.h is intentionally NOT included here -- it is a large single
// header, and every route handler below is expressed as a plain function
// returning a JSON/text string, so no httplib type needs to appear in this
// header at all. httplib.h is included only in server.cpp, where the
// routes are registered and its Request/Response objects are translated
// into calls to the methods below.
namespace httplib { class Server; class Request; class Response; }

namespace ocgdb {

class WebServer : public DbRead
{
public:
    WebServer() {}
    ~WebServer() override;

protected:
    void runTask() override;

    // Hook invoked by DbRead::readADb() (via the shared thread pool) for
    // each game while scanning a database for a PQL query. Collects
    // matching game IDs into pqlHits/succCount instead of printing them.
    void processAGameWithAThread(ThreadRecord* t, const bslib::PgnRecord& record,
                                  const std::vector<int8_t>& moveVec) override;

private:
    // ---- JSON-producing endpoint implementations (see server.cpp) ----
    // Query-string parameters are pre-extracted by the httplib route
    // lambdas in server.cpp and passed in as a plain string map, so none
    // of these need any httplib type. Callers (the route lambdas) must
    // already hold a shared lock on dbMutex for the duration of the call
    // -- see tryReadLock() below, which every route acquires before
    // calling into one of these.
    std::string apiInfoJson() const;
    std::string apiGamesJson(const std::map<std::string, std::string>& params) const;
    std::string apiGameDetailJson(int id, bool& found) const;
    std::string apiGamePgnText(int id, bool& found) const;
    std::string apiNameLookupJson(const char* table, const std::string& q, int limit) const;
    std::string apiStatsJson(bool refresh);
    // GET /api/player/:id -- W/D/L split by color, Elo-over-time (from
    // Games.WhiteElo/BlackElo, NOT Players.Elo -- see the comment above
    // its query in server.cpp for why), top opponents, top openings per
    // color, first/last game. Depends on the idx_games_white/idx_games_black
    // indexes (Phase 1.1, -index task) for reasonable performance on large
    // databases; still correct without them, just a full scan.
    std::string apiPlayerJson(int id, bool& found) const;
    // GET /api/h2h?a=&b= -- aggregate W/D/L between two specific players
    // plus their most recent games against each other.
    std::string apiH2hJson(int a, int b) const;
    // GET /api/event/:id -- every player who appears in this event, with
    // games/wins/draws/losses and score, sorted by score descending.
    std::string apiEventJson(int id, bool& found) const;
    std::string apiQueryJson(const std::string& pql, int limit);
    // GET /api/tree?fen=... -- next-move stats (games, W/D/L, avg Elo, most
    // recent year) for the position `fen` names, from the OpeningTree table
    // (see tree.h/.cpp for how -tree builds it). `fen` empty means the
    // standard starting position.
    std::string apiTreeJson(const std::string& fen) const;

    // Loads one game's tags + move list (SAN, UCI, per-ply FEN, comments)
    // using `board`, which the caller owns. Mirrors DbRead::queryForABoard
    // (dbread.cpp) but additionally requests ParseMoveListFlag_create_fen,
    // which none of the CLI tasks need. `board` and `record` are expected
    // to be local to the calling request/thread, so no locking beyond
    // dbMutex is needed (SQLite is compiled SQLITE_THREADSAFE=1 /
    // "serialized", so one connection may be used concurrently from many
    // threads as long as each thread uses its own prepared Statement
    // objects, which this function always does).
    bool loadGameDetail(int gameID, bslib::PgnRecord& record, bslib::BoardCore* board) const;

    // Column names actually present in Games, from PRAGMA table_info --
    // the schema is generated per-database from the source PGN tags, so
    // handlers must never assume a column exists.
    static std::vector<std::string> loadGamesColumnsFor(SQLite::Database* db);
    bool hasGamesColumn(const std::string& name) const;

    std::string findWebDir() const;

    // ---- the currently browsed database, switchable at runtime --------
    //
    // Everything the "light" read endpoints touch lives in one struct so
    // switching databases (or a background job temporarily taking one
    // over, see JobManager in jobs.h) is a single, atomic swap under
    // dbMutex rather than several fields that could observe a torn state.
    struct ActiveDb {
        SQLite::Database* db = nullptr;
        std::string path;
        SearchField searchField = SearchField::none;
        std::vector<std::string> gamesColumns;
        std::string statsCache; // /api/stats is expensive; cache per active db
    };
    ActiveDb active;
    mutable std::shared_mutex dbMutex;

    // Attempts to take dbMutex for reading without blocking. Every "light"
    // /api/* route calls this first and, on failure, responds 503 instead
    // of queuing behind a job that closed the active db to write to it
    // (see onJobStartCloseIfActive) -- a read that would otherwise stall
    // for as long as e.g. a -create run takes.
    bool tryReadLock(std::shared_lock<std::shared_mutex>& lock) const;

    // Callers must hold dbMutex (exclusively) already; these do not lock.
    bool openActiveDbLocked(const std::string& path, std::string& err);
    void closeActiveDbLocked();

    // Marks Info.DerivedStale = 1 on `wdb` (a writable connection the
    // caller already owns) -- called at the end of every write route
    // (Phase 4.3) so /api/info can tell the UI "GameMaterial/OpeningTree/
    // Evals/GameTree may no longer reflect the game data" without this
    // server trying to silently re-run any of those itself (they're
    // full-database rebuild tasks, not incremental).
    void markDerivedStaleLocked(SQLite::Database& wdb) const;
    // Closes the current active db (if any) and opens `path` in its place,
    // taking dbMutex itself. Used at startup and by
    // /api/admin/databases/activate.
    bool activateDatabase(const std::string& path, std::string& err);

    // JobManager callbacks (see jobs.h): if a job is about to write to the
    // database currently being browsed, release WebServer's own connection
    // to it first so the job's writes and any in-flight read never touch
    // the same file at once; reopen once the job finishes.
    void onJobStartCloseIfActive(const std::string& path);
    void onJobEndReopenIfActive(const std::string& path);

    // ---- admin auth / path safety --------------------------------------
    bool checkAdminAuth(const httplib::Request& req, httplib::Response& res) const;
    // Resolves `path`, rejects ".." components, and (when -root is set)
    // requires it to be inside paraRecord.rootDir. Used both directly by
    // admin routes and installed as JobManager::pathFilter.
    bool pathAllowed(const std::string& path, std::string& err) const;

    // ---- admin JSON endpoint implementations ---------------------------
    std::string adminStatusJson() const;
    std::string adminDatabasesJson() const;
    std::string adminJobsJson(const std::string& stateFilter) const;
    std::string adminJobDetailJson(int id, bool& found) const;
    std::string adminJobLogJson(int id, int64_t fromSeq) const;

private:
    // PQL scanning goes through the inherited DbRead::readADb(), which owns
    // its own transient SQLite::Database (assigned to the inherited `mDb`)
    // and drives the shared, static Core::pool. Only one scan runs at a
    // time (pqlMutex serializes /api/query requests), so mDb/searchField/
    // gameCnt are never touched by two requests concurrently.
    Parser parser;
    std::mutex pqlMutex;
    std::mutex pqlHitsMutex;
    std::vector<int> pqlHits;
    size_t pqlLimit = 50;

    std::mutex statsMutex;

    AdminStore* adminStore = nullptr;
    JobManager* jobManager = nullptr;
    std::string adminToken;

    httplib::Server* svr = nullptr;
};

} // namespace ocgdb

#endif /* OCGDB_SERVER_H */
