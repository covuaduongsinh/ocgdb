/**
 * This file is part of Open Chess Game Database Standard.
 *
 * See dbbackup.h.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include "dbbackup.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

#include "3rdparty/SQLiteCpp/SQLiteCpp.h"

namespace ocgdb {

namespace {

namespace fs = std::filesystem;

constexpr int kKeepCount = 5;
constexpr int64_t kThrottleSeconds = 3600; // ~1 hour per database, see dbbackup.h

std::mutex gThrottleMutex;
std::map<std::string, int64_t> gLastBackupAt; // resolved source path -> epoch seconds

int64_t nowEpochSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Sortable so the lexicographic filename ordering pruneOldBackups() relies
// on is also chronological order.
std::string timestampSuffix()
{
    auto t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return oss.str();
}

void pruneOldBackups(const fs::path& backupDir, const std::string& stem)
{
    std::vector<fs::path> matches;
    std::error_code ec;
    auto prefix = stem + ".";
    for (auto& entry : fs::directory_iterator(backupDir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0) matches.push_back(entry.path());
    }
    if ((int)matches.size() <= kKeepCount) return;

    std::sort(matches.begin(), matches.end());
    for (size_t i = 0; i + kKeepCount < matches.size(); i++) {
        std::error_code rmEc;
        fs::remove(matches[i], rmEc);
    }
}

} // namespace

bool backupIfDue(const std::string& dbPath)
{
    if (dbPath.empty()) return false;

    std::error_code ec;
    auto resolved = fs::weakly_canonical(dbPath, ec).string();
    if (resolved.empty()) resolved = dbPath;

    {
        std::lock_guard<std::mutex> lk(gThrottleMutex);
        auto now = nowEpochSeconds();
        auto it = gLastBackupAt.find(resolved);
        if (it != gLastBackupAt.end() && now - it->second < kThrottleSeconds) {
            return false;
        }
        gLastBackupAt[resolved] = now;
    }

    try {
        fs::path src(dbPath);
        auto backupDir = src.parent_path() / "backups";
        fs::create_directories(backupDir, ec);

        auto stem = src.stem().string();
        auto destPath = (backupDir / (stem + "." + timestampSuffix() + ".db3")).string();

        // Online/safe copy (SQLite's own backup API), not a raw file copy --
        // correct even if the source is in WAL mode or has another
        // in-process connection open against it (server.cpp's cached
        // read-only active.db, for one).
        SQLite::Database srcDb(dbPath, SQLite::OPEN_READONLY);
        srcDb.backup(destPath.c_str(), SQLite::Database::Save);

        pruneOldBackups(backupDir, stem);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Warning: backup skipped for '" << dbPath << "': " << e.what() << std::endl;
        return false;
    }
}

} // namespace ocgdb
