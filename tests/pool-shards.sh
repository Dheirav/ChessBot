#!/usr/bin/env bash
# Pool the summaries of a sharded gate into one result.
#
#   ./tests/pool-shards.sh <dir-of-shard-logs>
#
# Kept separate from shard-gate.sh so a run that was interrupted, or whose
# shards were started by hand, can still be scored.
set -u
DIR=${1:?directory of shard logs}

awk '
    # games   : 120  (W 40 / D 44 / L 36)
    /^games/  { gsub(/[^0-9 ]/, " "); n = split($0, f, " ")
                g += f[1]; w += f[2]; d += f[3]; l += f[4] }
    # pairs   : 60  (0-0.5-1-1.5-2: 3-9-30-12-6)
    /^pairs/  { gsub(/[^0-9 ]/, " "); n = split($0, f, " ")
                # f[1] = pair count, f[2..6] are the literal 0 0 5 1 1 5 2 of
                # the label, so the five counts are the last five fields.
                for (i = 0; i < 5; i++) p[i] += f[n - 4 + i]
                pairs += f[1] }
    END {
        if (g == 0) { print "no shard produced a result"; exit 1 }
        s = (w + d / 2) / g
        printf "\n=== pooled over %d shards ===\n", ARGC - 1
        printf "games   : %d  (W %d / D %d / L %d)\n", g, w, d, l
        printf "pairs   : %d  (0-0.5-1-1.5-2: %d-%d-%d-%d-%d)\n",
               pairs, p[0], p[1], p[2], p[3], p[4]
        printf "score   : %.2f%%\n", 100 * s

        # Pentanomial mean and variance, per pair, normalized to a per-game
        # score so it is comparable with the Elo scale.
        for (i = 0; i < 5; i++) {
            x = i / 4.0; q = p[i] / pairs
            mean += q * x; second += q * x * x
        }
        var = second - mean * mean
        if (var <= 0 || pairs == 0) {
            printf "Elo     : %+.1f   95%% CI unavailable (no variance in the sample)\n",
                   400 * log(s / (1 - s)) / log(10)
            exit 0
        }
        se = sqrt(var / pairs)
        lo = mean - 1.96 * se; hi = mean + 1.96 * se
        printf "Elo     : %+.1f   95%% CI [%+.1f, %+.1f]  (pentanomial)\n",
               400 * log(s / (1 - s)) / log(10),
               400 * log(lo / (1 - lo)) / log(10),
               400 * log(hi / (1 - hi)) / log(10)
    }
' "$DIR"/shard-*.log
