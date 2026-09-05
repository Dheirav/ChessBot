#!/usr/bin/env python3
"""Pack a gendata corpus into fixed-width arrays for training.

    tools/nnue-pack.py [corpus-dir] [--out DIR]

One shard in, one .npy set out, one process each. Done once because parsing
25M FENs in Python every epoch would cost more than the training does -- the
packed form is memory-mapped, so an epoch touches disk and not the parser.

Layout per position: up to 32 pieces as `piece_code * 64 + square` (0..767),
-1 padded, plus the count, side to move, score and result. Square numbering
matches the engine's `get1DIndex(file, rank)` -- rank 8 at index 0, so a1 is 56
-- because this indexing has to survive into the C++ accumulator later, and two
conventions that differ by a vertical flip produce a net that is silently wrong
rather than one that fails to load.
"""
import sys, os, glob, multiprocessing as mp
import numpy as np

MAXP = 32
PIECE = {'P':0,'N':1,'B':2,'R':3,'Q':4,'K':5}
RESULT = {'0.0':0, '0.5':1, '1.0':2}

def pack_shard(args):
    path, outdir = args
    n = sum(1 for _ in open(path, errors="replace"))
    base = os.path.join(outdir, os.path.splitext(os.path.basename(path))[0])
    feat = np.lib.format.open_memmap(base+".feat.npy", mode="w+", dtype=np.int16, shape=(n, MAXP))
    cnt  = np.lib.format.open_memmap(base+".cnt.npy",  mode="w+", dtype=np.uint8,  shape=(n,))
    stm  = np.lib.format.open_memmap(base+".stm.npy",  mode="w+", dtype=np.uint8,  shape=(n,))
    sc   = np.lib.format.open_memmap(base+".score.npy",mode="w+", dtype=np.int16,  shape=(n,))
    res  = np.lib.format.open_memmap(base+".res.npy",  mode="w+", dtype=np.uint8,  shape=(n,))
    i = 0
    with open(path, errors="replace") as fh:
        for line in fh:
            fen, s, r = line.rstrip("\n").split(";")
            parts = fen.split()
            sq = 0; k = 0
            row = feat[i]; row[:] = -1
            for ch in parts[0]:
                if ch == '/':
                    continue
                if ch.isdigit():
                    sq += int(ch); continue
                code = PIECE[ch.upper()] + (0 if ch.isupper() else 6)
                if k < MAXP:
                    row[k] = code * 64 + sq; k += 1
                sq += 1
            cnt[i] = k
            stm[i] = 0 if parts[1] == 'w' else 1
            v = int(s)
            sc[i]  = 32767 if v > 32767 else (-32768 if v < -32768 else v)
            res[i] = RESULT[r]
            i += 1
            if i % 250000 == 0:
                print(f"  {os.path.basename(path)}: {i:,}/{n:,}", flush=True)
    for a in (feat, cnt, stm, sc, res): a.flush()
    return path, i

if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    src = args[0] if args else "/home/dheirav/chessbot-data"
    out = sys.argv[sys.argv.index("--out")+1] if "--out" in sys.argv else os.path.join(src, "packed")
    os.makedirs(out, exist_ok=True)
    shards = sorted(glob.glob(os.path.join(src, "shard*.txt")))
    if not shards: sys.exit(f"no shards in {src}")
    print(f"packing {len(shards)} shards -> {out}", flush=True)
    with mp.Pool(len(shards)) as pool:
        for path, n in pool.imap_unordered(pack_shard, [(p, out) for p in shards]):
            print(f"done {os.path.basename(path)}: {n:,} positions", flush=True)
