/**
 * This file is part of Open Chess Game Database Standard.
 *
 * See admin.h.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include "admin.h"

#include <chrono>
#include <iostream>

using namespace ocgdb;

int64_t AdminStore::nowEpoch()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

AdminStore::AdminStore(const std::string& dbPath)
{
    try {
        db = new SQLite::Database(dbPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db->exec("PRAGMA journal_mode=WAL");

        execSql(
            "CREATE TABLE IF NOT EXISTS Databases ("
            " ID INTEGER PRIMARY KEY AUTOINCREMENT,"
            " Path TEXT UNIQUE NOT NULL,"
            " Label TEXT,"
            " AddedAt INTEGER,"
            " LastOpenedAt INTEGER,"
            " Note TEXT)");

        execSql(
            "CREATE TABLE IF NOT EXISTS Jobs ("
            " ID INTEGER PRIMARY KEY AUTOINCREMENT,"
            " Task TEXT,"
            " ParamsText TEXT,"
            " Cmdline TEXT,"
            " State TEXT,"
            " Progress INTEGER DEFAULT 0,"
            " ProgressTotal INTEGER DEFAULT 0,"
            " GameCnt INTEGER DEFAULT 0,"
            " ExitCode INTEGER DEFAULT 0,"
            " Error TEXT,"
            " CreatedAt INTEGER,"
            " StartedAt INTEGER,"
            " FinishedAt INTEGER)");

        execSql(
            "CREATE TABLE IF NOT EXISTS JobLog ("
            " JobID INTEGER,"
            " Seq INTEGER,"
            " Line TEXT,"
            " PRIMARY KEY(JobID, Seq))");

        execSql(
            "CREATE TABLE IF NOT EXISTS Settings ("
            " Name TEXT PRIMARY KEY,"
            " Value TEXT)");

    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore could not open/init '" << dbPath << "': " << e.what() << std::endl;
        delete db;
        db = nullptr;
    }
}

AdminStore::~AdminStore()
{
    delete db;
    db = nullptr;
}

void AdminStore::execSql(const std::string& sql)
{
    db->exec(sql);
}

// ------------------------------------------------------------- Databases

DbEntry AdminStore::addDatabase(const std::string& path, const std::string& label)
{
    std::lock_guard<std::mutex> lk(mutex_);
    DbEntry out;
    if (!db) return out;

    try {
        SQLite::Statement find(*db, "SELECT ID, Path, Label, AddedAt, LastOpenedAt, Note FROM Databases WHERE Path = ?");
        find.bind(1, path);
        if (find.executeStep()) {
            out.id = find.getColumn(0).getInt();
            out.path = find.getColumn(1).getString();
            out.label = find.getColumn(2).getString();
            out.addedAt = find.getColumn(3).getInt64();
            out.lastOpenedAt = find.getColumn(4).getInt64();
            out.note = find.getColumn(5).getString();
            return out;
        }

        auto now = nowEpoch();
        SQLite::Statement ins(*db, "INSERT INTO Databases (Path, Label, AddedAt, LastOpenedAt, Note) VALUES (?, ?, ?, 0, '')");
        ins.bind(1, path);
        ins.bind(2, label);
        ins.bind(3, now);
        ins.exec();

        out.id = (int)db->getLastInsertRowid();
        out.path = path;
        out.label = label;
        out.addedAt = now;
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::addDatabase: " << e.what() << std::endl;
    }
    return out;
}

bool AdminStore::removeDatabase(int id)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return false;
    try {
        SQLite::Statement stmt(*db, "DELETE FROM Databases WHERE ID = ?");
        stmt.bind(1, id);
        return stmt.exec() > 0;
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::removeDatabase: " << e.what() << std::endl;
        return false;
    }
}

static DbEntry rowToDbEntry(SQLite::Statement& q)
{
    DbEntry e;
    e.id = q.getColumn(0).getInt();
    e.path = q.getColumn(1).getString();
    e.label = q.getColumn(2).getString();
    e.addedAt = q.getColumn(3).getInt64();
    e.lastOpenedAt = q.getColumn(4).getInt64();
    e.note = q.getColumn(5).getString();
    return e;
}

std::vector<DbEntry> AdminStore::listDatabases() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<DbEntry> out;
    if (!db) return out;
    try {
        SQLite::Statement q(*db, "SELECT ID, Path, Label, AddedAt, LastOpenedAt, Note FROM Databases ORDER BY ID");
        while (q.executeStep()) out.push_back(rowToDbEntry(q));
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::listDatabases: " << e.what() << std::endl;
    }
    return out;
}

bool AdminStore::getDatabase(int id, DbEntry& out) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return false;
    try {
        SQLite::Statement q(*db, "SELECT ID, Path, Label, AddedAt, LastOpenedAt, Note FROM Databases WHERE ID = ?");
        q.bind(1, id);
        if (q.executeStep()) { out = rowToDbEntry(q); return true; }
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::getDatabase: " << e.what() << std::endl;
    }
    return false;
}

bool AdminStore::findDatabaseByPath(const std::string& path, DbEntry& out) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return false;
    try {
        SQLite::Statement q(*db, "SELECT ID, Path, Label, AddedAt, LastOpenedAt, Note FROM Databases WHERE Path = ?");
        q.bind(1, path);
        if (q.executeStep()) { out = rowToDbEntry(q); return true; }
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::findDatabaseByPath: " << e.what() << std::endl;
    }
    return false;
}

void AdminStore::touchDatabaseOpened(int id)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return;
    try {
        SQLite::Statement stmt(*db, "UPDATE Databases SET LastOpenedAt = ? WHERE ID = ?");
        stmt.bind(1, nowEpoch());
        stmt.bind(2, id);
        stmt.exec();
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::touchDatabaseOpened: " << e.what() << std::endl;
    }
}

void AdminStore::setDatabaseNote(int id, const std::string& note)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return;
    try {
        SQLite::Statement stmt(*db, "UPDATE Databases SET Note = ? WHERE ID = ?");
        stmt.bind(1, note);
        stmt.bind(2, id);
        stmt.exec();
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::setDatabaseNote: " << e.what() << std::endl;
    }
}

// -------------------------------------------------------------- Settings

std::string AdminStore::getSetting(const std::string& name, const std::string& def) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return def;
    try {
        SQLite::Statement q(*db, "SELECT Value FROM Settings WHERE Name = ?");
        q.bind(1, name);
        if (q.executeStep()) return q.getColumn(0).getString();
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::getSetting: " << e.what() << std::endl;
    }
    return def;
}

void AdminStore::setSetting(const std::string& name, const std::string& value)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return;
    try {
        SQLite::Statement stmt(*db, "INSERT INTO Settings (Name, Value) VALUES (?, ?) "
                                     "ON CONFLICT(Name) DO UPDATE SET Value = excluded.Value");
        stmt.bind(1, name);
        stmt.bind(2, value);
        stmt.exec();
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::setSetting: " << e.what() << std::endl;
    }
}

// ------------------------------------------------------------------ Jobs

int AdminStore::createJob(const std::string& task, const std::string& paramsText, const std::string& cmdline)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return -1;
    try {
        auto now = nowEpoch();
        SQLite::Statement stmt(*db,
            "INSERT INTO Jobs (Task, ParamsText, Cmdline, State, Progress, ProgressTotal, GameCnt, ExitCode, Error, CreatedAt, StartedAt, FinishedAt) "
            "VALUES (?, ?, ?, 'queued', 0, 0, 0, 0, '', ?, 0, 0)");
        stmt.bind(1, task);
        stmt.bind(2, paramsText);
        stmt.bind(3, cmdline);
        stmt.bind(4, now);
        stmt.exec();
        return (int)db->getLastInsertRowid();
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::createJob: " << e.what() << std::endl;
        return -1;
    }
}

void AdminStore::setJobRunning(int jobId)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return;
    try {
        SQLite::Statement stmt(*db, "UPDATE Jobs SET State = 'running', StartedAt = ? WHERE ID = ? AND State = 'queued'");
        stmt.bind(1, nowEpoch());
        stmt.bind(2, jobId);
        stmt.exec();
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::setJobRunning: " << e.what() << std::endl;
    }
}

void AdminStore::setJobProgress(int jobId, int64_t progress, int64_t progressTotal, int64_t gameCnt)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return;
    try {
        SQLite::Statement stmt(*db, "UPDATE Jobs SET Progress = ?, ProgressTotal = ?, GameCnt = ? WHERE ID = ?");
        stmt.bind(1, progress);
        stmt.bind(2, progressTotal);
        stmt.bind(3, gameCnt);
        stmt.bind(4, jobId);
        stmt.exec();
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::setJobProgress: " << e.what() << std::endl;
    }
}

void AdminStore::finishJob(int jobId, const std::string& state, int exitCode, const std::string& error)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return;
    try {
        SQLite::Statement stmt(*db, "UPDATE Jobs SET State = ?, ExitCode = ?, Error = ?, FinishedAt = ? WHERE ID = ?");
        stmt.bind(1, state);
        stmt.bind(2, exitCode);
        stmt.bind(3, error);
        stmt.bind(4, nowEpoch());
        stmt.bind(5, jobId);
        stmt.exec();
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::finishJob: " << e.what() << std::endl;
    }
}

bool AdminStore::cancelQueuedJob(int jobId)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return false;
    try {
        SQLite::Statement stmt(*db, "UPDATE Jobs SET State = 'cancelled', FinishedAt = ? WHERE ID = ? AND State = 'queued'");
        stmt.bind(1, nowEpoch());
        stmt.bind(2, jobId);
        return stmt.exec() > 0;
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::cancelQueuedJob: " << e.what() << std::endl;
        return false;
    }
}

static JobEntry rowToJobEntry(SQLite::Statement& q)
{
    JobEntry e;
    e.id = q.getColumn(0).getInt();
    e.task = q.getColumn(1).getString();
    e.paramsText = q.getColumn(2).getString();
    e.cmdline = q.getColumn(3).getString();
    e.state = q.getColumn(4).getString();
    e.progress = q.getColumn(5).getInt64();
    e.progressTotal = q.getColumn(6).getInt64();
    e.gameCnt = q.getColumn(7).getInt64();
    e.exitCode = q.getColumn(8).getInt();
    e.error = q.getColumn(9).getString();
    e.createdAt = q.getColumn(10).getInt64();
    e.startedAt = q.getColumn(11).getInt64();
    e.finishedAt = q.getColumn(12).getInt64();
    return e;
}

static const char* kJobColumns =
    "ID, Task, ParamsText, Cmdline, State, Progress, ProgressTotal, GameCnt, ExitCode, Error, CreatedAt, StartedAt, FinishedAt";

std::vector<JobEntry> AdminStore::listJobs(const std::string& stateFilter) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<JobEntry> out;
    if (!db) return out;
    try {
        std::string sql = std::string("SELECT ") + kJobColumns + " FROM Jobs";
        if (!stateFilter.empty()) sql += " WHERE State = ?";
        sql += " ORDER BY ID DESC";
        SQLite::Statement q(*db, sql);
        if (!stateFilter.empty()) q.bind(1, stateFilter);
        while (q.executeStep()) out.push_back(rowToJobEntry(q));
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::listJobs: " << e.what() << std::endl;
    }
    return out;
}

bool AdminStore::getJob(int jobId, JobEntry& out) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return false;
    try {
        std::string sql = std::string("SELECT ") + kJobColumns + " FROM Jobs WHERE ID = ?";
        SQLite::Statement q(*db, sql);
        q.bind(1, jobId);
        if (q.executeStep()) { out = rowToJobEntry(q); return true; }
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::getJob: " << e.what() << std::endl;
    }
    return false;
}

bool AdminStore::nextQueuedJob(JobEntry& out) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return false;
    try {
        std::string sql = std::string("SELECT ") + kJobColumns + " FROM Jobs WHERE State = 'queued' ORDER BY ID LIMIT 1";
        SQLite::Statement q(*db, sql);
        if (q.executeStep()) { out = rowToJobEntry(q); return true; }
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::nextQueuedJob: " << e.what() << std::endl;
    }
    return false;
}

void AdminStore::clearFinishedJobs()
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return;
    try {
        db->exec("DELETE FROM JobLog WHERE JobID IN (SELECT ID FROM Jobs WHERE State IN ('succeeded','failed','cancelled'))");
        db->exec("DELETE FROM Jobs WHERE State IN ('succeeded','failed','cancelled')");
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::clearFinishedJobs: " << e.what() << std::endl;
    }
}

// --------------------------------------------------------------- Job log

void AdminStore::appendLog(int jobId, const std::string& line)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db) return;
    try {
        SQLite::Statement next(*db, "SELECT COALESCE(MAX(Seq), 0) + 1 FROM JobLog WHERE JobID = ?");
        next.bind(1, jobId);
        int64_t seq = 1;
        if (next.executeStep()) seq = next.getColumn(0).getInt64();

        SQLite::Statement ins(*db, "INSERT INTO JobLog (JobID, Seq, Line) VALUES (?, ?, ?)");
        ins.bind(1, jobId);
        ins.bind(2, seq);
        ins.bind(3, line);
        ins.exec();

        if (seq > kMaxLogLinesPerJob) {
            SQLite::Statement trim(*db, "DELETE FROM JobLog WHERE JobID = ? AND Seq <= ?");
            trim.bind(1, jobId);
            trim.bind(2, seq - kMaxLogLinesPerJob);
            trim.exec();
        }
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::appendLog: " << e.what() << std::endl;
    }
}

std::vector<JobLogLine> AdminStore::getLog(int jobId, int64_t fromSeq) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<JobLogLine> out;
    if (!db) return out;
    try {
        SQLite::Statement q(*db, "SELECT Seq, Line FROM JobLog WHERE JobID = ? AND Seq > ? ORDER BY Seq");
        q.bind(1, jobId);
        q.bind(2, fromSeq);
        while (q.executeStep()) {
            JobLogLine l;
            l.seq = q.getColumn(0).getInt64();
            l.line = q.getColumn(1).getString();
            out.push_back(l);
        }
    } catch (SQLite::Exception& e) {
        std::cerr << "Error: AdminStore::getLog: " << e.what() << std::endl;
    }
    return out;
}
