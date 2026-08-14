#pragma once
//
// Drive an engine *binary* over UCI, so a match can compare two builds rather
// than two option sets inside one process.
//
// Why this exists (BUGS.md 8). tests/match's in-process mode varies
// SearchOptions, and evaluation is not a SearchOption — evaluate_details()
// reads the board and nothing else. So an evaluation change could not be
// expressed as an A/B at all. The only in-process way would have been to keep
// the superseded evaluation behind a flag, which means carrying known-wrong
// code purely so a match can see it; and even then both sides would have shared
// the process-global eval cache, so one side's scores would have been served to
// the other.
//
// Two processes share nothing: no eval cache, no transposition table, no
// globals. And the thing being compared is the binary that actually ships,
// rather than a flag that approximates it.
//
// Deliberately minimal. It speaks only the part of UCI this engine implements
// and this harness needs, and it trusts the child to be well behaved because
// the child is our own binary — a general-purpose UCI client would need
// timeouts, `stop` handling and protocol negotiation that would be dead code
// here.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

class UciEngine {
public:
    UciEngine() = default;
    ~UciEngine() { quit(); }

    UciEngine(const UciEngine&) = delete;
    UciEngine& operator=(const UciEngine&) = delete;

    // Spawns `path --uci` and completes the handshake. False if the binary
    // cannot be run or never answers, which is worth failing the run over: a
    // gate that silently fell back to one engine would compare a build to
    // itself and report a clean +0.
    //
    // `hashMb` is not optional in practice. UCI engines pick their own default
    // hash — this one takes 256 MB — and a sharded gate runs two of them per
    // shard. Twelve shards inheriting that default asked for 6 GB of
    // transposition table on a 7.7 GB machine and took WSL down with it, twice,
    // on 2026-08-14. Sizing it here also makes an external gate comparable to
    // an in-process one, which uses 32 MB a side.
    // `args` are the child's command-line arguments. ChessBot needs "--uci"
    // because its default mode opens a window; a standard UCI engine takes none
    // and will treat an unrecognised argument as a command to run and exit on.
    // That is not hypothetical — this hardcoded "--uci" and Stockfish died on
    // the first handshake, reporting itself as a broken pipe.
    bool start(const std::string& path, int hashMb = 32,
               const std::vector<std::string>& args = {"--uci"}) {
        int toChild[2], fromChild[2];
        if (pipe(toChild) != 0) return false;
        if (pipe(fromChild) != 0) { close(toChild[0]); close(toChild[1]); return false; }

        pid_ = fork();
        if (pid_ < 0) return false;

        if (pid_ == 0) {
            dup2(toChild[0], STDIN_FILENO);
            dup2(fromChild[1], STDOUT_FILENO);
            close(toChild[0]);  close(toChild[1]);
            close(fromChild[0]); close(fromChild[1]);
            // The engine's own chatter is not UCI and would only interleave
            // with the harness's output.
            FILE* devnull = fopen("/dev/null", "w");
            if (devnull) dup2(fileno(devnull), STDERR_FILENO);
            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(path.c_str()));
            for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
            argv.push_back(nullptr);
            execv(path.c_str(), argv.data());
            _exit(127);   // exec failed; the parent sees the handshake time out
        }

        close(toChild[0]);
        close(fromChild[1]);
        out_ = fdopen(toChild[1], "w");
        in_  = fdopen(fromChild[0], "r");
        if (!in_ || !out_) return false;

        send("uci");
        if (!waitFor("uciok")) return false;
        send("setoption name Hash value " + std::to_string(hashMb));
        send("isready");
        return waitFor("readyok");
    }

    void newGame() {
        send("ucinewgame");
        send("isready");
        waitFor("readyok");
    }

    // `moves` is the whole game in UCI notation from the initial position, which
    // is how UCI expresses a game: a position command is a complete statement,
    // not a delta. It is also what lets the engine see repetitions (BUGS.md 1).
    //
    // Returns the bestmove string, or "" if the child died.
    std::string bestMove(const std::vector<std::string>& moves,
                         long moveTimeMs, uint64_t maxNodes, int maxDepth) {
        std::string pos = "position startpos";
        if (!moves.empty()) {
            pos += " moves";
            for (const std::string& m : moves) pos += " " + m;
        }
        send(pos);

        std::string go = "go";
        if (maxNodes > 0)  go += " nodes " + std::to_string(maxNodes);
        if (moveTimeMs > 0) go += " movetime " + std::to_string(moveTimeMs);
        if (maxNodes == 0 && moveTimeMs == 0) go += " depth " + std::to_string(maxDepth);
        send(go);

        haveScore_ = false;
        pv_.clear();
        std::string line;
        while (readLine(line)) {
            if (line.compare(0, 10, "info depth") == 0) {
                captureScore(line);
            } else if (line.compare(0, 8, "bestmove") == 0) {
                size_t sp = line.find(' ');
                if (sp == std::string::npos) return "";
                std::string mv = line.substr(sp + 1);
                size_t end = mv.find(' ');           // ignore any "ponder ..."
                if (end != std::string::npos) mv.resize(end);
                return mv;
            }
        }
        return "";
    }

    // Score of the last completed iteration, in centipawns from the side to
    // move's point of view — the same convention the in-process path uses, so
    // the adjudication logic does not care which mode produced it.
    bool haveScore() const { return haveScore_; }
    int  lastScore() const { return lastScore_; }

    // The principal variation of the last completed iteration, in UCI moves.
    //
    // Needed because a static evaluation cannot explain a search score at the
    // position where the search started: the point of a tactic is that the
    // material changes several plies later. Anything attributing a score to
    // named evaluation terms has to walk to the end of this line first.
    const std::vector<std::string>& lastPv() const { return pv_; }

    void quit() {
        if (pid_ <= 0) return;
        if (out_) { send("quit"); fclose(out_); out_ = nullptr; }
        if (in_)  { fclose(in_);  in_ = nullptr; }
        int status = 0;
        for (int i = 0; i < 50; ++i) {          // ~500 ms to exit on its own
            if (waitpid(pid_, &status, WNOHANG) == pid_) { pid_ = -1; return; }
            usleep(10000);
        }
        kill(pid_, SIGKILL);
        waitpid(pid_, &status, 0);
        pid_ = -1;
    }

private:
    pid_t pid_ = -1;
    FILE* in_ = nullptr;
    FILE* out_ = nullptr;
    int  lastScore_ = 0;
    bool haveScore_ = false;
    std::vector<std::string> pv_;

    void send(const std::string& s) {
        if (!out_) return;
        fputs(s.c_str(), out_);
        fputc('\n', out_);
        fflush(out_);
    }

    bool readLine(std::string& line) {
        if (!in_) return false;
        char buf[4096];
        if (!fgets(buf, sizeof buf, in_)) return false;
        line.assign(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        return true;
    }

    bool waitFor(const char* token) {
        std::string line;
        while (readLine(line))
            if (line.compare(0, strlen(token), token) == 0) return true;
        return false;   // pipe closed: the child died or never started
    }

    // "info depth 7 score cp -338 nodes ..." or "score mate 3". A mate is
    // mapped onto the same scale the in-process search reports, so the resign
    // adjudicator treats both modes identically.
    void capturePv(const std::string& line) {
        const size_t p = line.find(" pv ");
        if (p == std::string::npos) return;
        pv_.clear();
        std::istringstream in(line.substr(p + 4));
        std::string mv;
        while (in >> mv) pv_.push_back(mv);
    }

    void captureScore(const std::string& line) {
        capturePv(line);
        size_t p = line.find(" score ");
        if (p == std::string::npos) return;
        p += 7;
        if (line.compare(p, 3, "cp ") == 0) {
            lastScore_ = std::atoi(line.c_str() + p + 3);
            haveScore_ = true;
        } else if (line.compare(p, 5, "mate ") == 0) {
            int n = std::atoi(line.c_str() + p + 5);
            lastScore_ = (n >= 0) ? (30000 - n * 2) : (-30000 - n * 2);
            haveScore_ = true;
        }
    }
};
