#!/usr/bin/env bash
# Stop the Lichess bot after N more games, or after a delay — never mid-game.
#
# "Stop after 1 more game" means the game on the board right now is the last
# one. Run it with no arguments for a menu; the flags are for scripting:
#
#   ./lichess/bot-stop.sh --games 10
#   ./lichess/bot-stop.sh --minutes 90
#   ./lichess/bot-stop.sh --at 23:30
#   ./lichess/bot-stop.sh --now              stop after the game in progress
#   ./lichess/bot-stop.sh --status           show state and exit
#   ... --quiet                              no live display, for nohup
#
# This exists because stopping the bot correctly is five rules that are easy to
# get wrong and expensive when you do, and each has been got wrong here:
#
#   1. A bare SIGINT does NOT let the game in progress finish (BUGS.md 7). It
#      stops playing and exits, leaving our clock running with nobody to
#      answer. That cost eight minutes of a rated game once and a -120 forfeit
#      another time. It is only safe to signal during a game when lichess-bot
#      was started with `quit_after_all_games_finish: true`, which makes the
#      first signal mean "play this one out, take nothing new, then exit".
#   2. That option is read at startup, so what governs is the config the
#      running process was started with — never the file as it reads now.
#      Editing config.yml does not reach a bot that is already running, so this
#      script checks the config named on the process's own command line and
#      distrusts it if the file was touched after the process began.
#   3. Counting game records is a proxy for "games finished", and the proxy
#      slips in both directions: no record is written for a game the bot was
#      killed out of, and a game the bot reconnects to gets its record written
#      at the disconnect as well as at the end. Either one makes a count-based
#      `--games 1` stop land a game late — which is what it did. So the count
#      only decides *when to signal*; it never decides what is safe.
#   4. `pgrep -f chessbot` matches the shell running the check (BUGS.md 9) and
#      has produced false positives repeatedly. Only `pgrep -x` on the command
#      name is trustworthy.
#   5. Checking for the engine *after* signalling proves nothing when it finds
#      nothing, because the engine exits as a consequence of the signal. It
#      proves a great deal when it finds something: an engine still alive ten
#      seconds after a graceful signal is the game surviving it, which is the
#      only direct test of rule 1 that does not cost a rated game to run.
set -uo pipefail

ARCHIVE="${ARCHIVE:-/home/dheirav/Code/lichess-bot/game_records}"
CLEAR_POLLS=3        # one clear poll is not a gap; games can follow fast
SURVIVE_SECS=10      # how long to watch the engine after a graceful signal
MODE="" TARGET="" QUIET=0

# One mode, stated once. Taking the last flag silently is how `--status
# --games 5` arms a real stop while reading like a query -- which happened the
# first time this script was run, against a live bot.
setmode() {
    [ -z "$MODE" ] || { echo "refusing: --$1 conflicts with --$MODE; state one" >&2; exit 1; }
    MODE=$1
}
while [ $# -gt 0 ]; do
    case "$1" in
        --games)   setmode games;   TARGET="${2:?--games needs a number}"; shift 2 ;;
        --minutes) setmode minutes; TARGET="${2:?--minutes needs a number}"; shift 2 ;;
        --at)      setmode at;      TARGET="${2:?--at needs HH:MM}"; shift 2 ;;
        --now)     setmode now;     shift ;;
        --status)  setmode status;  shift ;;
        --quiet)   QUIET=1;         shift ;;
        --help|-h) sed -n '2,13p' "$0" | sed 's/^# \?//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

botpid()   { ps -eo pid,args --no-headers | awk '$2 ~ /python/ && /lichess-bot\.py/ {print $1; exit}'; }
gamelive() { pgrep -x chessbot >/dev/null; }
gamecount(){ ls "$ARCHIVE"/*.pgn 2>/dev/null | wc -l; }

# The config the process was started with, not the one this repo holds now.
botconfig() {
    tr '\0' '\n' < "/proc/$1/cmdline" 2>/dev/null |
        awk '/^--config=/ { sub(/^--config=/, ""); print; exit }
             /^--config$/ { getline; print; exit }'
}

# graceful — a signal during a game will let that game finish.
# stale     — the config says so but was edited after the bot started, so the
#             running process may still be on the old value. Assume the worst.
# legacy    — the bot was started without it; only a gap between games is safe.
stopmode() {
    local cfg started
    cfg=$(botconfig "$1")
    [ -n "$cfg" ] && [ -r "$cfg" ] || { echo legacy; return; }
    grep -Eq '^[[:space:]]*quit_after_all_games_finish:[[:space:]]*true' "$cfg" || { echo legacy; return; }
    started=$(date -d "$(ps -p "$1" -o lstart=)" +%s 2>/dev/null) || started=0
    if [ "$started" -gt 0 ] && [ "$(stat -c %Y "$cfg")" -gt "$started" ]; then echo stale; else echo graceful; fi
}

header() {
    printf '\033[H\033[J'
    printf '  ┌──────────────────────────────────────────────┐\n'
    printf '  │   lichess bot — scheduled stop               │\n'
    printf '  └──────────────────────────────────────────────┘\n\n'
}

state() {
    local p=$(botpid) n=$(gamecount)
    printf '  bot        %s\n' "$([ -n "$p" ] && echo "running (pid $p)" || echo "NOT RUNNING")"
    if [ -n "$p" ]; then
        case "$(stopmode "$p")" in
            graceful) printf '  stop       exact — the game in progress finishes, then it exits\n' ;;
            stale)    printf '  stop       inexact — config.yml was edited after this bot started;\n'
                      printf '             restart it to get exact stops\n' ;;
            legacy)   printf '  stop       inexact — this bot was started without\n'
                      printf '             quit_after_all_games_finish, so a stop can only be taken\n'
                      printf '             in a gap and may land a game or more late\n' ;;
        esac
    fi
    printf '  game now   %s\n' "$(gamelive && echo 'in progress' || echo 'none')"
    printf '  games      %d archived\n' "$n"
}

menu() {
    while true; do
        header; state
        printf '\n'
        printf '   1)  stop after a number of games (1 = the one being played now)\n'
        printf '   2)  stop after a number of minutes\n'
        printf '   3)  stop at a time today (HH:MM)\n'
        printf '   4)  stop after the game in progress\n'
        printf '   5)  refresh\n'
        printf '   q)  quit, changing nothing\n\n'
        printf '  choose> '
        read -r c || { echo; exit 0; }
        case "$c" in
            1) printf '  how many more games? '; read -r TARGET
               case "$TARGET" in ''|*[!0-9]*) printf '  not a number.\n'; sleep 1; continue ;; esac
               [ "$TARGET" -gt 0 ] || { printf '  at least one.\n'; sleep 1; continue; }
               MODE=games; return ;;
            2) printf '  how many minutes? '; read -r TARGET
               case "$TARGET" in ''|*[!0-9]*) printf '  not a number.\n'; sleep 1; continue ;; esac
               MODE=minutes; return ;;
            3) printf '  at what time (HH:MM)? '; read -r TARGET
               date -d "today $TARGET" >/dev/null 2>&1 || { printf '  not a time.\n'; sleep 1; continue; }
               MODE=at; return ;;
            4) MODE=now; return ;;
            5) continue ;;
            q|Q) printf '\n  nothing changed. The bot is still running.\n'; exit 0 ;;
            *) continue ;;
        esac
    done
}

[ -n "$MODE" ] || menu
if [ "$MODE" = status ]; then header; state; exit 0; fi

PID=$(botpid)
[ -n "$PID" ] || { echo "the bot is not running — nothing to stop"; exit 1; }
GRACEFUL=0; [ "$(stopmode "$PID")" = graceful ] && GRACEFUL=1
START_GAMES=$(gamecount)
START_TS=$(date +%s)
case "$MODE" in
    minutes) DEADLINE=$((START_TS + TARGET * 60)) ;;
    at)      DEADLINE=$(date -d "today $TARGET" +%s); [ "$DEADLINE" -le "$START_TS" ] && DEADLINE=$((DEADLINE + 86400)) ;;
    *)       DEADLINE=0 ;;
esac
played() { echo $(( $(gamecount) - START_GAMES )); }

plan() {
    case "$MODE" in
        games)      printf '  stopping   after %d more games (%d to go)\n' "$TARGET" "$((TARGET - $(played)))" ;;
        minutes|at) printf '  stopping   at %s (%d min away)\n' "$(date -d "@$DEADLINE" '+%H:%M')" "$(( (DEADLINE - $(date +%s) + 59) / 60 ))" ;;
        now)        printf '  stopping   %s\n' "$([ "$GRACEFUL" = 1 ] && echo 'after the game in progress' || echo 'at the next gap between games')" ;;
    esac
    [ "$GRACEFUL" = 1 ] || printf '  note       inexact: this bot only takes a stop in a gap (see --status)\n'
}

# When the signal will be honoured. A graceful bot is signalled *during* the
# last game, because that is what makes it the last game: the record count
# reaching the target means the game we wanted has already ended and the next
# one may well have started.
reached() {
    case "$MODE" in
        games)      if [ "$GRACEFUL" = 1 ]; then
                        [ "$(played)" -ge "$TARGET" ] ||
                        { [ "$(played)" -eq $((TARGET - 1)) ] && gamelive; }
                    else
                        [ "$(played)" -ge "$TARGET" ]
                    fi ;;
        minutes|at) [ "$(date +%s)" -ge "$DEADLINE" ] ;;
        now)        [ "$GRACEFUL" = 1 ] || true ;;
    esac
}

trap 'printf "\n  cancelled — the bot is still running.\n"; exit 0' INT
while ! reached; do
    if [ "$QUIET" != 1 ]; then
        header; state; printf '\n'; plan
        printf '\n  armed. Ctrl-C cancels and leaves the bot running.\n'
    fi
    sleep 10
done

# Past this point the signal is going out and cannot be taken back.
trap 'printf "\n  too late to cancel — the stop is already sent.\n"; exit 0' INT

if [ "$GRACEFUL" = 1 ]; then
    LIVE=0; gamelive && LIVE=1
    BEFORE=$(gamecount)
    printf '  signalling bot %s%s\n' "$PID" \
        "$([ "$LIVE" = 1 ] && echo ' — it will play the current game out first')"
    kill -INT "$PID" 2>/dev/null
    # Never a second signal here: the second one is force-quit, mid-game.
    if [ "$LIVE" = 1 ]; then
        sleep "$SURVIVE_SECS"
        if gamelive; then
            printf '  the game survived the signal (engine still alive after %ss)\n' "$SURVIVE_SECS"
        elif [ "$(gamecount)" -gt "$BEFORE" ]; then
            printf '  the game ended on its own within those %ss\n' "$SURVIVE_SECS"
        else
            printf '\n  *** THE ENGINE IS GONE AND NO GAME RECORD APPEARED ***\n'
            printf '  The game may have been dropped mid-play, which forfeits it on time.\n'
            printf '  Restart the bot NOW — it reconnects and resumes (BUGS.md 7):\n'
            printf '    ./lichess/run.sh\n\n'
            exit 1
        fi
    fi
    while ps -p "$PID" >/dev/null 2>&1; do
        if [ "$QUIET" != 1 ]; then
            header; state; printf '\n'
            printf '  stop sent. Waiting for the bot to finish and exit.\n'
        fi
        sleep 10
    done
else
    printf '\n  condition met — waiting for a gap between games...\n'
    clear=0
    while [ "$clear" -lt "$CLEAR_POLLS" ]; do
        if gamelive; then clear=0; else clear=$((clear + 1)); fi
        sleep 1
    done
    printf '  no game live (%d clear polls) — stopping bot %s\n' "$CLEAR_POLLS" "$PID"
    kill -INT "$PID" 2>/dev/null
    sleep 5
    # A second signal is only safe here, where the first check established that
    # no game was running before anything was sent.
    ps -p "$PID" >/dev/null 2>&1 && { kill -INT "$PID" 2>/dev/null; sleep 5; }
    if ps -p "$PID" >/dev/null 2>&1; then printf '  WARNING: bot %s is still up\n' "$PID"; exit 1; fi
fi

printf '  bot stopped. %d games played since armed.\n' "$(played)"
last=$(ls -t "$ARCHIVE"/*.pgn 2>/dev/null | head -1)
[ -n "$last" ] && printf '  last game: %s — %s\n' "$(basename "$last" .pgn)" \
    "$(grep -h '^\[Termination' "$last" | sed 's/.*"\(.*\)".*/\1/')"
