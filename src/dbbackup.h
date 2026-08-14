/**
 * This file is part of Open Chess Game Database Standard.
 *
 * Best-effort hot backup for -server's write paths (Admin UX Phase 4).
 * See dbbackup.cpp.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#pragma once

#include <string>

namespace ocgdb {

// Best-effort hot backup of dbPath into a sibling "backups/" directory,
// using SQLiteCpp's online Backup API (safe to run against a database that
// may be in WAL mode or mid-use elsewhere in-process) rather than a raw
// file copy. Throttled globally per resolved path: a call within
// kThrottleSeconds of the last one for the same path is a no-op. Prunes
// older backups for the same database, keeping only the most recent
// kKeepCount. Never throws -- any failure is logged to stderr and
// swallowed, so callers can call this unconditionally before a risky write
// without it ever blocking or failing that write.
//
// Returns true if a backup file was actually written this call.
bool backupIfDue(const std::string& dbPath);

} // namespace ocgdb
