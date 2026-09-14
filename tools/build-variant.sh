#!/usr/bin/env bash
# Build the engine with extra compile-time flags into its own directory, so a
# compile-time candidate (an evaluation term behind a -D constant) can be
# benched and gated against the shipped binary without touching it.
#
#   ./tools/build-variant.sh build/ks300 -DKING_DANGER_SCALE_PCT=300
#
# Produces <dir>/chessbot, <dir>/chessbot-uci.sh, <dir>/bench, <dir>/bbequiv,
# <dir>/evalerror, <dir>/evalref and <dir>/evaldump. Search toggles have runtime options and go through
# shard-gate.sh; this is for the constants that do not, where A and B have to
# be two binaries (tests/match --engineA/--engineB).
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=${1:?usage: build-variant.sh <dir> [CXXFLAGS...]}; shift
EXTRA="$*"
CXX=${CXX:-g++}
CXXFLAGS="-std=c++17 -O2 -Wall -pthread -I./src $EXTRA"
JOBS=${JOBS:-$(nproc)}

mkdir -p "$OUT/obj"
compile() {
    local src=$1 obj="$OUT/obj/$(echo "$1" | tr / _ | sed 's/\.cpp$/.o/')"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ -n "${FORCE:-}" ]; then
        $CXX $CXXFLAGS -c "$src" -o "$obj"
    fi
}
export -f compile; export CXX CXXFLAGS OUT FORCE
printf '%s\n' src/*.cpp src/engine/*.cpp src/gui/*.cpp tests/bench.cpp tests/bbequiv.cpp tests/evalerror.cpp tests/evalref.cpp tools/evaldump.cpp \
    | xargs -P "$JOBS" -I{} bash -c 'compile {}'

ENGINE=$(ls "$OUT"/obj/src_engine_*.o)
$CXX "$OUT"/obj/src_*.o -o "$OUT/chessbot" -lsfml-graphics -lsfml-window -lsfml-system -pthread
for t in bench bbequiv evalerror evalref; do
    $CXX "$OUT/obj/tests_$t.o" $ENGINE -o "$OUT/$t" -pthread
done
$CXX "$OUT/obj/tools_evaldump.o" $ENGINE -o "$OUT/evaldump" -pthread
# The engine resolves its lookup tables at <exe dir>/src/engine/lookup_data.
mkdir -p "$OUT/src/engine"
ln -sfn "$(pwd)/src/engine/lookup_data" "$OUT/src/engine/lookup_data"
cat > "$OUT/chessbot-uci.sh" <<'SH'
#!/usr/bin/env bash
cd "$(dirname "$0")" || exit 1
exec ./chessbot --uci "$@"
SH
chmod +x "$OUT/chessbot-uci.sh"
echo "built $OUT with: $EXTRA"
