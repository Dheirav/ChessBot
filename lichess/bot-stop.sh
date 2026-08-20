#!/usr/bin/env bash
# Stop the Lichess bot after N more games, or after a delay — never mid-game.
#
# Run it with no arguments for a menu. The flags are for scripting:
#
#   ./lichess/bot-stop.sh --games 10
#   ./lichess/bot-stop.sh --minutes 90
#   ./lichess/bot-stop.sh --at 23:30
#   ./lichess/bot-stop.sh --now              stop at the next gap
#   ./lichess/bot-stop.sh --status           show state and exit
#   ... --quiet                              no live display, for nohup
#
# This exists because stopping the bot correctly is four rules that are easy to
# get wrong and expensive when you do, and each has been got wrong here:
#
#   1. SIGINT does NOT let the game in progress finish (BUGS.md 7). It stops
#      playing and exits, leaving our clock running with nobody to answer. That
#      cost eight minutes of a rated game once and a -120 forfeit another time.
#      So the signal may only be sent when no game is live.
#   2. `pgrep -f chessbot` matches the shell running the check (BUGS.md 9) and
#      has produced false positives repeatedly. Only `pgrep -x` on the command
#      name is trustworthy.
#   3. Checking *after* signalling proves nothing, because the engine exits as a
#      consequence of the signal. The check has to precede the action.
#   4. One clear poll is not a gap. Games follow each other within a second or
#      two, so a single reading can catch the handover between one engine
#      exiting and the next starting. Three consecutive readings is the
#      difference between "between games" and "caught mid-handover".
set -uo pipefail

ARCHIVE="${ARCHIVE:-/home/dheirav/Code/lichess-bot/game_records}"
CLEAR_POLLS=3
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
        --help|-h) sed -n '2,10p' "$0" | sed 's/^# \?//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

botpid()   { ps -eo pid,args --no-headers | awk '$2 ~ /python/ && /lichess-bot\.py/ {print $1; exit}'; }
gamelive() { pgrep -x chessbot >/dev/null; }
gamecount(){ ls "$ARCHIVE"/*.pgn 2>/dev/null | wc -l; }

header() {
    printf '\033[H\033[J'
    printf '  ┌──────────────────────────────────────────────┐\n'
    printf '  │   lichess bot — scheduled stop               │\n'
    printf '  └──────────────────────────────────────────────┘\n\n'
}

state() {
    local p=$(botpid) n=$(gamecount)
    printf '  bot        %s\n' "$([ -n "$p" ] && echo "running (pid $p)" || echo "NOT RUNNING")"
    printf '  game now   %s\n' "$(gamelive && echo 'in progress — a stop will wait for it' || echo 'none')"
    printf '  games      %d archived\n' "$n"
}

menu() {
    while true; do
        header; state
        printf '\n'
        printf '   1)  stop after a number of games\n'
        printf '   2)  stop after a number of minutes\n'
        printf '   3)  stop at a time today (HH:MM)\n'
        printf '   4)  stop at the next gap between games\n'
        printf '   5)  refresh\n'
        printf '   q)  quit, changing nothing\n\n'
        printf '  choose> '
        read -r c || { echo; exit 0; }
        case "$c" in
            1) printf '  how many more games? '; read -r TARGET
               case "$TARGET" in ''|*[!0-9]*) printf '  not a number.\n'; sleep 1; continue ;; esac
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
START_GAMES=$(gamecount)
START_TS=$(date +%s)
case "$MODE" in
    minutes) DEADLINE=$((START_TS + TARGET * 60)) ;;
    at)      DEADLINE=$(date -d "today $TARGET" +%s); [ "$DEADLINE" -le "$START_TS" ] && DEADLINE=$((DEADLINE + 86400)) ;;
    *)       DEADLINE=0 ;;
esac

plan() {
    case "$MODE" in
        games)      printf '  stopping   after %d more games (%d to go)\n' "$TARGET" "$((TARGET - ($(gamecount) - START_GAMES)))" ;;
        minutes|at) printf '  stopping   at %s (%d min away)\n' "$(date -d "@$DEADLINE" '+%H:%M')" "$(( (DEADLINE - $(date +%s) + 59) / 60 ))" ;;
        now)        printf '  stopping   at the next gap between games\n' ;;
    esac
}

reached() {
    case "$MODE" in
        games)      [ $(( $(gamecount) - START_GAMES )) -ge "$TARGET" ] ;;
        minutes|at) [ "$(date +%s)" -ge "$DEADLINE" ] ;;
        now)        true ;;
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

[ "$QUIET" = 1 ] || printf '\n  condition met — waiting for a gap between games...\n'
clear=0
while [ "$clear" -lt "$CLEAR_POLLS" ]; do
    if gamelive; then clear=0; else clear=$((clear + 1)); fi
    sleep 1
done

printf '  no game live (%d clear polls) — stopping bot %s\n' "$CLEAR_POLLS" "$PID"
kill -INT "$PID" 2>/dev/null
sleep 5
ps -p "$PID" >/dev/null 2>&1 && { kill -INT "$PID" 2>/dev/null; sleep 5; }
if ps -p "$PID" >/dev/null 2>&1; then printf '  WARNING: bot %s is still up\n' "$PID"; exit 1; fi

printf '  bot stopped. %d games played since armed.\n' "$(( $(gamecount) - START_GAMES ))"
last=$(ls -t "$ARCHIVE"/*.pgn 2>/dev/null | head -1)
[ -n "$last" ] && printf '  last game: %s — %s\n' "$(basename "$last" .pgn)" \
    "$(grep -h '^\[Termination' "$last" | sed 's/.*"\(.*\)".*/\1/')"
