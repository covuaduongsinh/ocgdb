/**
 * This file is part of Open Chess Game Database Standard.
 *
 * EngineProcess drives a UCI-speaking chess engine (Stockfish or similar)
 * as a child process via ChildProcess (process.h), handling the
 * uci -> uciok / isready -> readyok handshake and the "go" -> "info" ...
 * "bestmove" search loop. Used by the -server task's POST /api/analyse
 * (server.cpp) to stream live analysis to the browser.
 *
 * Security note: this class runs whatever executable it's pointed at,
 * with no sandboxing. Registering an engine (see AdminStore::addEngine(),
 * admin.h, and its /api/admin/engines/add route, server.cpp) is
 * deliberately equivalent to "run this arbitrary program" -- acceptable
 * under OCGDB's personal/localhost threat model (the server only ever
 * binds 127.0.0.1), NOT if this server were ever exposed beyond loopback.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#ifndef OCGDB_ENGINE_H
#define OCGDB_ENGINE_H

#include <string>
#include <functional>
#include <cstdint>

#include "process.h"

namespace ocgdb {

// One "info depth ... score ... pv ..." line, loosely parsed -- only the
// fields the analysis UI needs; anything else in the line is left alone
// (not an error, just not surfaced as its own field).
struct UciInfo {
    int depth = 0;
    int multipv = 1;
    int scoreCp = 0;     // centipawns; meaningless if mate is true
    bool mate = false;
    int mateIn = 0;      // moves to mate, sign indicates which side (UCI convention)
    int64_t nodes = 0;
    int64_t nps = 0;
    std::string pv;      // space-separated UCI (coordinate) moves, e.g. "e2e4 e7e5 g1f3"
};

class EngineProcess
{
public:
    ~EngineProcess() { stop(); }

    // Launches enginePath (no arguments -- UCI engines take none) and
    // performs the uci -> uciok handshake. Returns false (errorString
    // set) on failure. Note: ChildProcess::readLine() is blocking with no
    // timeout, so a executable that doesn't speak UCI (or hangs) will
    // hang this call too -- there is no watchdog here. This is why engine
    // paths go through pathAllowed() at registration (server.cpp): the
    // set of executables that can reach this class is meant to be
    // something the user deliberately chose, not attacker-controlled.
    bool start(const std::string& enginePath);

    // isready -> readyok.
    bool isReady();

    void setOption(const std::string& name, const std::string& value);
    void newGame();
    // Empty fen = standard starting position ("position startpos"),
    // otherwise "position fen <fen>".
    void setPositionFen(const std::string& fen);

    // Sends "go depth N" if depth > 0, else "go movetime N" if
    // movetimeMs > 0, else a 1-second movetime search as a safe default
    // (never an unbounded "go infinite" -- there is no interactive "stop"
    // button wired up on the OCGDB side beyond shouldStop()/disconnection
    // below). Blocks, calling onInfo for each parsed "info ..." line as
    // it arrives, until "bestmove ..." is read. If shouldStop is given
    // and returns true (checked between lines -- see the blocking caveat
    // on start() above, the same applies here), sends "stop" and keeps
    // reading until the engine's own "bestmove" response, per the UCI
    // protocol's requirement that a stopped search still ends with one.
    // Returns the bestmove token (e.g. "e2e4"), or empty on error/EOF.
    std::string go(int depth, int movetimeMs,
                    const std::function<void(const UciInfo&)>& onInfo,
                    const std::function<bool()>& shouldStop = nullptr);

    // "quit" + close stdin for a clean shutdown, falling back to
    // terminate() if the process doesn't exit on its own. Safe to call
    // more than once / on a never-started instance.
    void stop();

    bool isRunning() const { return proc.isRunning(); }

    std::string errorString;

private:
    ChildProcess proc;
    bool started = false;
};

} // namespace ocgdb

#endif /* OCGDB_ENGINE_H */
