/**
 * This file is part of Open Chess Game Database Standard.
 *
 * See process.h. Two independent implementations behind #ifdef _WIN32 --
 * process creation/pipe/signal APIs are inherently platform-specific, this
 * is the one place in the whole -server feature that needs it.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include "process.h"

#include <cstring>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ocgdb {

static std::wstring utf8ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    auto n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string wideToUtf8(const std::wstring& w)
{
    if (w.empty()) return std::string();
    auto n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::string getExecutablePath()
{
    std::vector<wchar_t> buf(1024);
    for (;;) {
        auto n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (n == 0) return std::string();
        if (n < buf.size() - 1) return wideToUtf8(std::wstring(buf.data(), n));
        buf.resize(buf.size() * 2);
    }
}

// Quotes a single argv element following the rules the Microsoft C runtime
// uses to *parse* a command line back into argv (CommandLineToArgvW) --
// this is the quoting every Windows CreateProcess caller must follow, not
// an OCGDB-specific convention.
static std::wstring quoteArg(const std::wstring& arg)
{
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring out = L"\"";
    for (size_t i = 0; i < arg.size(); ) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') { backslashes++; i++; }
        if (i == arg.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += L'"';
        } else {
            out.append(backslashes, L'\\');
            out += arg[i];
        }
        i++;
    }
    out += L'"';
    return out;
}

static std::wstring buildCommandLine(const std::string& exePath, const std::vector<std::string>& args)
{
    std::wstring cmd = quoteArg(utf8ToWide(exePath));
    for (auto& a : args) {
        cmd += L' ';
        cmd += quoteArg(utf8ToWide(a));
    }
    return cmd;
}

ChildProcess::~ChildProcess()
{
    if (running) terminate();
    closeHandles();
}

void ChildProcess::closeHandles()
{
    if (hReadPipe) { CloseHandle((HANDLE)hReadPipe); hReadPipe = nullptr; }
    if (hThread) { CloseHandle((HANDLE)hThread); hThread = nullptr; }
    if (hProcess) { CloseHandle((HANDLE)hProcess); hProcess = nullptr; }
}

bool ChildProcess::start(const std::string& exePath, const std::vector<std::string>& args)
{
    errorString.clear();

    HANDLE readPipe = nullptr, writePipe = nullptr;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        errorString = "CreatePipe failed";
        return false;
    }
    // The parent's read end must NOT be inherited by the child.
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};

    auto cmdLine = buildCommandLine(exePath, args);
    // CreateProcessW may write into this buffer, so it must not be a
    // read-only literal.
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    auto exeW = utf8ToWide(exePath);

    BOOL ok = CreateProcessW(
        exeW.c_str(),
        cmdBuf.data(),
        nullptr, nullptr,
        TRUE, // inherit handles
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi);

    CloseHandle(writePipe);

    if (!ok) {
        errorString = "CreateProcessW failed, error " + std::to_string(GetLastError());
        CloseHandle(readPipe);
        return false;
    }

    CloseHandle(pi.hThread);
    hProcess = pi.hProcess;
    hThread = nullptr;
    hReadPipe = readPipe;
    running = true;
    terminated = false;
    exitCode = -1;
    return true;
}

bool ChildProcess::readLine(std::string& line)
{
    for (;;) {
        auto nl = lineBuf.find('\n');
        if (nl != std::string::npos) {
            line = lineBuf.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lineBuf.erase(0, nl + 1);
            return true;
        }
        if (!hReadPipe) {
            if (!lineBuf.empty()) {
                line = lineBuf;
                lineBuf.clear();
                return true;
            }
            return false;
        }
        char buf[4096];
        DWORD n = 0;
        BOOL ok = ReadFile((HANDLE)hReadPipe, buf, sizeof(buf), &n, nullptr);
        if (!ok || n == 0) {
            CloseHandle((HANDLE)hReadPipe);
            hReadPipe = nullptr;
            continue;
        }
        lineBuf.append(buf, n);
    }
}

void ChildProcess::terminate()
{
    if (hProcess && running && !terminated) {
        TerminateProcess((HANDLE)hProcess, 1);
        terminated = true;
    }
    wait();
}

int ChildProcess::wait()
{
    if (hProcess && running) {
        WaitForSingleObject((HANDLE)hProcess, INFINITE);
        DWORD code = 0;
        if (GetExitCodeProcess((HANDLE)hProcess, &code)) {
            exitCode = terminated ? -1 : (int)code;
        }
        running = false;
    }
    return exitCode;
}

} // namespace ocgdb

#else // POSIX

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <climits>
#include <cstdlib>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace ocgdb {

std::string getExecutablePath()
{
#if defined(__APPLE__)
    // No /proc on macOS; _NSGetExecutablePath is the standard replacement.
    // First call (with a null buffer) reports the required size, including
    // the terminating NUL.
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) return std::string();

    std::vector<char> buf(size);
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return std::string();

    // _NSGetExecutablePath may return a path containing symlinks (e.g. a
    // Homebrew shim); resolve it the same way readlink("/proc/self/exe")
    // already gives a fully-resolved path on Linux below.
    char resolved[PATH_MAX];
    if (realpath(buf.data(), resolved)) {
        return std::string(resolved);
    }
    return std::string(buf.data());
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return std::string();
    buf[n] = 0;
    return std::string(buf);
#endif
}

ChildProcess::~ChildProcess()
{
    if (running) terminate();
}

void ChildProcess::closeHandles()
{
    if (readFd >= 0) { close(readFd); readFd = -1; }
}

bool ChildProcess::start(const std::string& exePath, const std::vector<std::string>& args)
{
    errorString.clear();

    int fds[2];
    if (pipe(fds) != 0) {
        errorString = "pipe() failed";
        return false;
    }

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(exePath.c_str()));
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t child = fork();
    if (child < 0) {
        errorString = "fork() failed";
        close(fds[0]);
        close(fds[1]);
        return false;
    }

    if (child == 0) {
        // Child: merge stdout+stderr into the write end, close the read end.
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        execv(exePath.c_str(), argv.data());
        _exit(127); // execv only returns on failure
    }

    // Parent
    close(fds[1]);
    readFd = fds[0];
    pid = child;
    running = true;
    terminated = false;
    exitCode = -1;
    return true;
}

bool ChildProcess::readLine(std::string& line)
{
    for (;;) {
        auto nl = lineBuf.find('\n');
        if (nl != std::string::npos) {
            line = lineBuf.substr(0, nl);
            lineBuf.erase(0, nl + 1);
            return true;
        }
        if (readFd < 0) {
            if (!lineBuf.empty()) {
                line = lineBuf;
                lineBuf.clear();
                return true;
            }
            return false;
        }
        char buf[4096];
        ssize_t n = read(readFd, buf, sizeof(buf));
        if (n <= 0) {
            close(readFd);
            readFd = -1;
            continue;
        }
        lineBuf.append(buf, (size_t)n);
    }
}

void ChildProcess::terminate()
{
    if (pid > 0 && running && !terminated) {
        kill(pid, SIGTERM);
        terminated = true;
    }
    wait();
}

int ChildProcess::wait()
{
    if (pid > 0 && running) {
        int status = 0;
        waitpid(pid, &status, 0);
        if (terminated) {
            exitCode = -1;
        } else if (WIFEXITED(status)) {
            exitCode = WEXITSTATUS(status);
        } else {
            exitCode = -1;
        }
        running = false;
    }
    return exitCode;
}

} // namespace ocgdb

#endif
