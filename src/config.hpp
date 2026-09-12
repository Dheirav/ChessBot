#pragma once

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// Central place for all engine/game settings. Defaults live here, and an
// optional plain-text config file (default "chessbot.conf") can override them.
//
// Supported file keys (one per line, `key = value`, # starts a comment):
//   move_time_ms                = <integer, 0 disables the clock>
//   search_depth                = <1..64>
//   transposition_table_size_mb = <integer>
//   log_evaluations             = <on|off>
//   time_control                = <minutes>[+<seconds>] | none
//
// Example chessbot.conf:
//   # Engine settings
//   moveTimeMs = 3000
//   searchDepth = 8
//   transpositionTableSizeMB = 256
//   logEvaluations = off
//   timeControl = 5+3
struct Settings {
    // Wall-clock budget per move. This, not searchDepth, is what normally ends
    // the engine's search: the same depth costs milliseconds in an endgame and
    // seconds in a dense middlegame, so a fixed depth either wastes time or
    // runs over. Set to 0 to search purely by depth.
    int moveTimeMs = 3000;

    // Ceiling on iterations, and the only limit when moveTimeMs is 0. Raised
    // from 5: depth 5 costs ~1.1s per move where depth 8 costs ~2.7s, so the
    // old default was discarding almost the whole of the 22x speedup measured
    // in BACKLOG.md section 7.
    int searchDepth = 8;

    // A time control for GUI games, in the notation every chess site uses:
    // "5+3" is five minutes a side with three seconds added after each move,
    // "10" is ten minutes and no increment. Both sides get the same clock,
    // the human's runs while it is their turn, and a flag is a loss.
    //
    // Absent or "none": untimed, which is how the GUI has always played, with
    // the engine spending moveTimeMs per move and the clocks counting up.
    // With a control set, moveTimeMs is ignored: the engine's budget for each
    // move comes from its remaining clock through the same allocation rule
    // the UCI path uses against lichess, so it plays the clock the way the
    // bot does.
    long timeControlBaseMs = 0;   // 0 = untimed
    long timeControlIncMs  = 0;

    int transpositionTableSizeMB = 256;

    // Dump every evaluation term, per move, to evaluation_log_<epoch>.txt.
    //
    // Off by default. It used to be unconditional, so every run of the app left
    // a new file in whatever directory it was started from, and nothing read
    // them: tests/evalref covers the same ground deliberately, over 24k
    // positions with a stored reference to diff against. This is now a debug
    // switch for looking at one game by hand.
    bool logEvaluations = false;
};

namespace detail {

inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Map a config key to a Settings field. Returns true if the key was recognized.
inline bool applyKey(const std::string& key, const std::string& value, Settings& s) {
    if (key == "moveTimeMs" || key == "move_time_ms") {
        int v = std::atoi(value.c_str());
        if (v >= 0) s.moveTimeMs = v;
        return true;
    }
    if (key == "timeControl" || key == "time_control") {
        // "<minutes>[+<seconds>]", or "none". Fractional minutes allowed
        // ("0.5+1" is a thirty-second game), because short controls are
        // useful for testing the clock itself.
        if (value == "none" || value == "off" || value == "0") {
            s.timeControlBaseMs = 0;
            s.timeControlIncMs = 0;
            return true;
        }
        const size_t plus = value.find('+');
        const double base = std::atof(value.substr(0, plus).c_str());
        const double inc = (plus == std::string::npos) ? 0.0
                         : std::atof(value.substr(plus + 1).c_str());
        if (base > 0) {
            s.timeControlBaseMs = (long)(base * 60000.0);
            s.timeControlIncMs = (long)(inc * 1000.0);
        }
        return true;
    }
    if (key == "searchDepth" || key == "search_depth") {
        int v = std::atoi(value.c_str());
        // The old cap of 10 existed because nothing bounded the search but the
        // depth. With a clock, a high ceiling is how you let a fast position
        // search deep, so the cap is now just the killer/history table size.
        if (v > 0 && v <= 64) s.searchDepth = v;
        return true;
    }
    if (key == "transpositionTableSizeMB" || key == "transposition_table_size_mb") {
        int v = std::atoi(value.c_str());
        if (v >= 8) s.transpositionTableSizeMB = v;
        return true;
    }
    if (key == "logEvaluations" || key == "log_evaluations") {
        s.logEvaluations = (value == "1" || value == "true" || value == "yes" ||
                            value == "on");
        return true;
    }
    return false;
}

}  // namespace detail

// Load settings from an optional config file. Missing/unreadable/unparseable
// files leave the defaults in place.
inline Settings loadSettings(const std::string& path = "chessbot.conf") {
    Settings s;
    std::ifstream in(path);
    if (!in.is_open()) return s;

    std::string line;
    while (std::getline(in, line)) {
        // Strip comments and blank lines
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = detail::trim(line);
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = detail::trim(line.substr(0, eq));
        std::string value = detail::trim(line.substr(eq + 1));
        if (!detail::applyKey(key, value, s)) {
            std::cerr << "[config] Ignoring unknown key: " << key << std::endl;
        }
    }
    return s;
}