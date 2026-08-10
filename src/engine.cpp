/**
 * This file is part of Open Chess Game Database Standard.
 *
 * See engine.h.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#include "engine.h"

#include <sstream>

using namespace ocgdb;

namespace {

UciInfo parseInfoLine(const std::string& line)
{
    UciInfo info;
    std::istringstream iss(line);
    std::string tok;
    iss >> tok; // "info"
    while (iss >> tok) {
        if (tok == "depth") {
            iss >> info.depth;
        } else if (tok == "multipv") {
            iss >> info.multipv;
        } else if (tok == "score") {
            std::string kind;
            iss >> kind;
            if (kind == "cp") {
                iss >> info.scoreCp;
                info.mate = false;
            } else if (kind == "mate") {
                iss >> info.mateIn;
                info.mate = true;
            }
        } else if (tok == "nodes") {
            iss >> info.nodes;
        } else if (tok == "nps") {
            iss >> info.nps;
        } else if (tok == "pv") {
            std::string rest;
            std::getline(iss, rest);
            auto p = rest.find_first_not_of(' ');
            info.pv = (p == std::string::npos) ? std::string() : rest.substr(p);
            break; // pv runs to end of line by convention
        }
        // Every other UCI info sub-command (seldepth, time, hashfull,
        // tbhits, currmove, currmovenumber, cpuload, string, ...) takes
        // exactly one value token, which is simply left unconsumed here --
        // it's read as `tok` on the *next* loop iteration, matches none
        // of the branches above, and is silently skipped. The tokenizer
        // stays in sync either way since every iteration reads exactly
        // one token via `iss >> tok`.
    }
    return info;
}

} // namespace

bool EngineProcess::start(const std::string& enginePath)
{
    errorString.clear();
    started = false;

    if (!proc.start(enginePath, {})) {
        errorString = proc.errorString;
        return false;
    }

    if (!proc.writeLine("uci")) {
        errorString = "could not write to engine's stdin";
        return false;
    }

    std::string line;
    while (proc.readLine(line)) {
        if (line == "uciok") {
            started = true;
            return true;
        }
    }

    errorString = "engine exited or closed its output before sending uciok";
    return false;
}

bool EngineProcess::isReady()
{
    if (!started || !proc.writeLine("isready")) return false;

    std::string line;
    while (proc.readLine(line)) {
        if (line == "readyok") return true;
    }
    return false;
}

void EngineProcess::setOption(const std::string& name, const std::string& value)
{
    if (!started) return;
    proc.writeLine("setoption name " + name + " value " + value);
}

void EngineProcess::newGame()
{
    if (!started) return;
    proc.writeLine("ucinewgame");
}

void EngineProcess::setPositionFen(const std::string& fen)
{
    if (!started) return;
    proc.writeLine(fen.empty() ? "position startpos" : "position fen " + fen);
}

std::string EngineProcess::go(int depth, int movetimeMs,
                               const std::function<void(const UciInfo&)>& onInfo,
                               const std::function<bool()>& shouldStop)
{
    if (!started) return std::string();

    std::string cmd = "go";
    if (depth > 0) cmd += " depth " + std::to_string(depth);
    else if (movetimeMs > 0) cmd += " movetime " + std::to_string(movetimeMs);
    else cmd += " movetime 1000"; // never an unbounded "go infinite" -- see engine.h

    if (!proc.writeLine(cmd)) return std::string();

    bool stopSent = false;
    std::string line;
    while (proc.readLine(line)) {
        if (line.rfind("info ", 0) == 0) {
            if (onInfo) onInfo(parseInfoLine(line));
        } else if (line.rfind("bestmove", 0) == 0) {
            std::istringstream iss(line);
            std::string tok, best;
            iss >> tok >> best; // "bestmove" "<move>" ["ponder" "<move>"]
            return best;
        }

        if (!stopSent && shouldStop && shouldStop()) {
            proc.writeLine("stop");
            stopSent = true; // UCI still requires a bestmove after "stop" -- keep reading for it
        }
    }
    return std::string();
}

void EngineProcess::stop()
{
    if (!proc.isRunning()) return;
    if (started) proc.writeLine("quit");
    proc.closeStdin();
    proc.terminate(); // no-op if the engine already exited on "quit"
    started = false;
}
