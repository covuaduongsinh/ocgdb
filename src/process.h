/**
 * This file is part of Open Chess Game Database Standard.
 *
 * ChildProcess runs another executable (in practice, this program itself,
 * re-invoked with a different task) and streams its combined stdout/stderr
 * back line by line. Used by JobManager (src/jobs.h) to run -create/-merge/
 * -export/-dup/-bench/-q as cancellable background jobs, fully isolated from
 * the WebServer process: a job that crashes or hangs cannot take the server
 * down with it, and Core::pool (a process-wide static, see core.h) is never
 * shared between the two.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#ifndef OCGDB_PROCESS_H
#define OCGDB_PROCESS_H

#include <string>
#include <vector>

namespace ocgdb {

// Returns the absolute path to the currently running executable (this
// program's own binary), so jobs can re-invoke it with different arguments.
// Empty string on failure.
std::string getExecutablePath();

class ChildProcess
{
public:
    ChildProcess() = default;
    ~ChildProcess();

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // Launches exePath with args (args[0] should NOT repeat the exe path --
    // this mirrors execv's argv[1..], not argv[0..]). stdout and stderr of
    // the child are merged into a single pipe read via readLine(), so log
    // lines come back in true chronological order. Returns false (and sets
    // errorString) if the process could not be started.
    bool start(const std::string& exePath, const std::vector<std::string>& args);

    // Blocks until a full line (without trailing newline) is available, or
    // the child closes its output pipe. Returns false at end-of-stream.
    bool readLine(std::string& line);

    // Writes `line` followed by '\n' to the child's stdin (for line-based
    // protocols such as UCI -- see engine.h). Returns false if the process
    // isn't running or the write failed (the pipe end could already be
    // closed if the child exited).
    bool writeLine(const std::string& line);

    // Closes the write end of the child's stdin, signalling EOF to it.
    // Many command-line protocols (UCI included) treat this the same as
    // an explicit "quit": safe to call before terminate() for a clean
    // shutdown instead of an abrupt kill.
    void closeStdin();

    // Requests the child to stop (SIGTERM on POSIX, TerminateProcess on
    // Windows) and reaps it. Safe to call from a thread other than the one
    // that called start()/readLine(). No-op if the process already exited.
    void terminate();

    // Blocks until the child exits (if not already) and returns its exit
    // code, or -1 if the process was terminate()'d or never started.
    int wait();

    bool isRunning() const { return running; }

    std::string errorString;

private:
    void closeHandles();

    bool running = false;
    bool terminated = false;
    int exitCode = -1;
    std::string lineBuf;

#ifdef _WIN32
    void* hProcess = nullptr;
    void* hThread = nullptr;
    void* hReadPipe = nullptr;
    void* hWritePipe = nullptr;
#else
    int pid = -1;
    int readFd = -1;
    int writeFd = -1;
#endif
};

} // namespace ocgdb

#endif /* OCGDB_PROCESS_H */
