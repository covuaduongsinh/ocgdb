/**
 * This file is part of Open Chess Game Database Standard.
 *
 * JobManager runs OCGDB CLI tasks (create/merge/export/dup/bench/query) as
 * background child processes (see process.h), one at a time, queued and
 * tracked in AdminStore (see admin.h). This is how the -server control
 * plane lets a browser trigger the same work the command line does,
 * without ever running a second Core/Builder/etc. in-process: Core::pool
 * (core.h) is a process-wide static, and WebServer already owns one Core
 * instance for its own lifetime, so a second one sharing/deleting that pool
 * from another thread would be unsafe. A separate process sidesteps that
 * entirely.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#ifndef OCGDB_JOBS_H
#define OCGDB_JOBS_H

#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>

#include "admin.h"
#include "process.h"

namespace ocgdb {

// Turns one submitted (task, params) pair into a validated argv for a
// fresh `ocgdb` invocation. Pure/no I/O beyond std::filesystem existence
// checks and the caller-supplied pathFilter -- this is the entire trusted
// boundary between "strings a browser sent" and "a real child process
// command line", so every parameter lands here through a fixed, named slot
// (never string-concatenated) and every path-shaped value is run through
// pathFilter (e.g. WebServer's -root containment check) before use.
bool buildJobArgv(const std::string& task,
                   const std::map<std::string, std::string>& params,
                   const std::function<bool(const std::string& path, std::string& err)>& pathFilter,
                   std::vector<std::string>& argv,
                   std::string& paramsText,
                   std::string& writeTargetPath, // the .db3 this job WRITES to, if any; empty if read-only
                   std::string& err);

class JobManager
{
public:
    ~JobManager();

    // exePath: this program's own binary (see getExecutablePath() in
    // process.h) -- every job runs as `exePath <argv...>`.
    void start(AdminStore* store, const std::string& exePath);
    void stop();

    // Applied to every path-shaped parameter before a job is allowed to
    // queue (e.g. WebServer's -root containment + ".." rejection). nullptr
    // (the default) allows everything.
    std::function<bool(const std::string& path, std::string& err)> pathFilter;

    // Validates params, persists+enqueues the job, and wakes the worker.
    // Returns the new job ID, or -1 with `err` set if params were invalid.
    int submit(const std::string& task, const std::map<std::string, std::string>& params, std::string& err);

    // Cancels a still-queued job outright, or terminates the child if
    // jobId is the one currently running. Returns false if the job has
    // already finished (or never existed).
    bool cancel(int jobId);

    // The job currently running, if any (-1 otherwise). Used by WebServer
    // to report which job a 503 "busy" response is waiting on.
    int activeJob() const { return activeJobId.load(); }

    // Fired from the worker thread, right before/after a job that WRITES
    // to `path` runs -- lets WebServer release/reacquire its own read
    // connection to that same file so the job's writes and the server's
    // reads are never touching the file at the same instant.
    std::function<void(const std::string& path)> onJobStart;
    std::function<void(const std::string& path)> onJobEnd;

private:
    struct QueuedJob {
        int id = -1;
        std::vector<std::string> argv;
        std::string writeTargetPath;
    };

    void workerLoop();
    void runOne(const QueuedJob& job);

    AdminStore* store = nullptr;
    std::string exePath;

    std::thread worker;
    std::condition_variable cv;
    std::atomic<bool> shuttingDown{false};

    std::mutex queueMutex;
    std::map<int, QueuedJob> queued; // ordered by job ID == submission order

    std::mutex childMutex;
    ChildProcess* activeChild = nullptr; // non-null only while a job's process is alive
    std::atomic<int> activeJobId{-1};
};

} // namespace ocgdb

#endif /* OCGDB_JOBS_H */
