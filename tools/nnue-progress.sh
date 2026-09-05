#!/usr/bin/env bash
# Progress for the NNUE pipeline: packing, then training.
SC=/tmp/claude-1000/-home-dheirav-Code-ChessBot/514fe6b5-51aa-473f-8f02-cfcd855cd27c/scratchpad
D=${1:-/home/dheirav/chessbot-data}
printf '\n'
if pgrep -f 'nnue-pack.py' >/dev/null; then
    printf '  PHASE 1/2  packing corpus -> arrays\n\n'
    for s in 1 2 3 4; do
        line=$(grep -o "shard$s.txt: [0-9,]*/[0-9,]*" "$SC/pack.log" 2>/dev/null | tail -1)
        [ -n "$line" ] && printf '    %s\n' "$line"
    done
    printf '\n    packed so far: %s\n' "$(du -sh "$D/packed" 2>/dev/null | cut -f1)"
elif pgrep -f 'nnue-train.py' >/dev/null; then
    printf '  PHASE 2/2  training  (phase 1 done)\n\n'
    tr '\r' '\n' < "$SC/train.log" 2>/dev/null | grep -v '^ *$' | tail -6 | sed 's/^/    /'
else
    printf '  nothing running\n\n'
    [ -f "$SC/train.log" ] && tr '\r' '\n' < "$SC/train.log" | grep -v '^ *$' | tail -6 | sed 's/^/    /'
fi
printf '\n'
