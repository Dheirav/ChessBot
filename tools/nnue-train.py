#!/usr/bin/env python3
"""Train a first NNUE on a packed gendata corpus.

    tools/nnue-train.py [--data DIR] [--epochs 8] [--batch 16384] [--out net.pt]

Architecture: 768 -> 256 per perspective -> concat 512 -> 32 -> 32 -> 1.

Plain piece-square input rather than HalfKP, deliberately. HalfKP is stronger
and it is also 40 960 features and a king-bucketed accumulator; the hard part of
this project is not the net, it is making the accumulator update incrementally
inside makeMove/unmakeMove on a path the profiler measured at 1.87 billion
Piece::type() calls. A first net exists to answer whether the corpus supports
one at all, and it should be the simplest thing that can answer that.

**Validation is shard 4, held out whole.** The corpus records no game ids, so a
line-level split is impossible -- and would be wrong anyway: positions from one
game share a result label, so splitting by position leaks the label across the
split and flatters the number. tools/texel-corpus.py documents the same trap.
A shard is a different seed and therefore entirely different games.

**Both labels are used**, blended: the score is dense and slightly wrong
everywhere, the result is sparse and right. Weighting only the result is what
made the Texel fit memorise 267 games.
"""
import sys, os, glob, time, argparse
import numpy as np
import torch
import torch.nn as nn

PAD = 768          # embedding row reserved for padding
ACTIVE = 32        # typical active features per perspective; sets the FT init scale
SCALE = 400.0      # centipawns -> sigmoid, the Texel K by another name


def load(split_dir, shards):
    out = {}
    for k, dt in (("feat", np.int16), ("cnt", np.uint8), ("stm", np.uint8),
                  ("score", np.int16), ("res", np.uint8)):
        parts = [np.load(os.path.join(split_dir, f"{s}.{k}.npy"), mmap_mode="r") for s in shards]
        out[k] = parts
    return out


class Net(nn.Module):
    def __init__(self, hidden=256):
        super().__init__()
        # padding_idx keeps the pad row at zero and out of the gradient, so a
        # position with 12 pieces and one with 32 accumulate the same way.
        self.ft = nn.EmbeddingBag(PAD + 1, hidden, mode="sum", padding_idx=PAD)
        self.ft_bias = nn.Parameter(torch.zeros(hidden))
        self.l1 = nn.Linear(2 * hidden, 32)
        self.l2 = nn.Linear(32, 32)
        self.l3 = nn.Linear(32, 1)

        # The feature transformer *sums* ~32 rows, so its init cannot be the
        # default N(0,1) that a Linear would get. The first run did exactly that
        # and the summed accumulator landed in -23..+23 against a clipped-ReLU
        # window of [0,1]: 89% of activations saturated, no gradient reached the
        # embedding, and the net finished 1.3% better than predicting a
        # constant. Scale so a typical 32-piece sum lands inside the window.
        nn.init.normal_(self.ft.weight, mean=0.0, std=0.6 / (ACTIVE ** 0.5))
        with torch.no_grad():
            self.ft.weight[PAD].zero_()
            self.ft_bias.fill_(0.5)

    def forward(self, w_idx, b_idx, stm):
        aw = self.ft(w_idx) + self.ft_bias
        ab = self.ft(b_idx) + self.ft_bias
        # Side to move first. The accumulator in C++ will be built the same way,
        # and a net trained with the other order is silently mirrored.
        us    = torch.where(stm.unsqueeze(1) == 0, aw, ab)
        them  = torch.where(stm.unsqueeze(1) == 0, ab, aw)
        x = torch.clamp(torch.cat([us, them], dim=1), 0.0, 1.0)   # clipped ReLU
        x = torch.clamp(self.l1(x), 0.0, 1.0)
        x = torch.clamp(self.l2(x), 0.0, 1.0)
        # Scaled, and this is not cosmetic. l3 sees 32 inputs clamped to [0,1],
        # so its raw output is bounded by sum|w| -- about +/-50 with any sane
        # init. The labels are centipawn scores spanning +/-800. Without this
        # factor the net cannot *express* the target at any weight setting
        # reachable by training, and it converges to a constant: the first two
        # runs of this file both finished at 307.5cp MAE against 311.6 for
        # predicting the mean. Raw output stays O(1), which is also the range a
        # quantised int8 forward pass will want later.
        return self.l3(x).squeeze(1) * SCALE


def perspectives(feat, dev):
    """White- and black-perspective feature indices from the packed form."""
    f = feat.to(dev).long()
    valid = f >= 0
    code = torch.div(f, 64, rounding_mode="floor")
    sq = f % 64
    w = torch.where(valid, f, torch.full_like(f, PAD))
    bcode = (code + 6) % 12
    b = torch.where(valid, bcode * 64 + (sq ^ 56), torch.full_like(f, PAD))
    return w, b


def batches(data, batch, dev, shuffle=True, limit=None):
    feats, cnts, stms, scs, ress = (data["feat"], data["cnt"], data["stm"],
                                    data["score"], data["res"])
    for fi in range(len(feats)):
        n = len(cnts[fi])
        if limit: n = min(n, limit)
        order = np.random.permutation(n) if shuffle else np.arange(n)
        for i in range(0, n - batch + 1, batch):
            idx = np.sort(order[i:i + batch])
            f = torch.from_numpy(np.asarray(feats[fi][idx]))
            stm = torch.from_numpy(np.asarray(stms[fi][idx]).astype(np.int64)).to(dev)
            sc = torch.from_numpy(np.asarray(scs[fi][idx]).astype(np.float32)).to(dev)
            rs = torch.from_numpy(np.asarray(ress[fi][idx]).astype(np.float32)).to(dev) / 2.0
            w, b = perspectives(f, dev)
            yield w, b, stm, sc, rs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="/home/dheirav/chessbot-data/packed")
    ap.add_argument("--epochs", type=int, default=8)
    ap.add_argument("--batch", type=int, default=16384)
    ap.add_argument("--lam", type=float, default=0.7, help="weight on the score label")
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--out", default="/home/dheirav/chessbot-data/net.pt")
    a = ap.parse_args()

    shards = sorted({os.path.basename(p).split(".")[0]
                     for p in glob.glob(os.path.join(a.data, "shard*.feat.npy"))})
    if len(shards) < 2: sys.exit(f"need >=2 shards in {a.data}, found {shards}")
    train_s, val_s = shards[:-1], shards[-1:]
    print(f"train {train_s}   validate {val_s} (held out whole: different seed, different games)")

    dev = "cuda" if torch.cuda.is_available() else "cpu"
    tr, va = load(a.data, train_s), load(a.data, val_s)
    ntrain = sum(len(c) for c in tr["cnt"])
    print(f"device {dev}   train {ntrain:,} positions   val {sum(len(c) for c in va['cnt']):,}")

    net = Net().to(dev)

    # A saturated accumulator does not fail, it trains to a constant -- which is
    # what the first run of this file did. It is invisible in a loss curve, so
    # it gets checked here instead, before an hour is spent on it.
    with torch.no_grad():
        w0, b0, s0, _, _ = next(batches(va, 4096, dev, shuffle=False))
        acc = net.ft(w0) + net.ft_bias
        live = ((acc > 0) & (acc < 1)).float().mean().item()
        print(f"  accumulator live fraction at init: {live*100:.1f}% "
              f"(range {acc.min():.2f}..{acc.max():.2f})")
        # Second instrument, for the second bug: a net whose output cannot span
        # the labels trains to a constant just as quietly as a saturated one.
        reach = (net.l3.weight.abs().sum().item() + abs(net.l3.bias.item())) * SCALE
        p95 = float(np.percentile(np.abs(np.asarray(va["score"][0][:200000])), 95))
        print(f"  output reach at init: +/-{reach:.0f} cp   labels |p95| = {p95:.0f} cp")
        if reach < p95:
            sys.exit(f"REFUSING TO TRAIN: the network's maximum reachable output is "
                     f"{reach:.0f}cp but labels reach {p95:.0f}cp; it cannot express "
                     f"the target and will converge to a constant.")

        if live < 0.25:
            sys.exit(f"REFUSING TO TRAIN: only {live*100:.1f}% of the accumulator is "
                     f"inside clamp(0,1); the feature-transformer init is wrong and "
                     f"the net will learn a constant.")

    opt = torch.optim.AdamW(net.parameters(), lr=a.lr)
    steps = ntrain // a.batch
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=a.lr,
                                                total_steps=steps * a.epochs + 1)
    t0 = time.time()
    for ep in range(1, a.epochs + 1):
        net.train(); seen = 0; run = 0.0
        for w, b, stm, sc, rs in batches(tr, a.batch, dev):
            target = a.lam * torch.sigmoid(sc / SCALE) + (1 - a.lam) * rs
            pred = torch.sigmoid(net(w, b, stm) / SCALE)
            loss = ((pred - target) ** 2).mean()
            opt.zero_grad(set_to_none=True); loss.backward(); opt.step(); sched.step()
            run += loss.item(); seen += 1
            if seen % 100 == 0:
                el = time.time() - t0
                done = (ep - 1) * steps + seen
                eta = (steps * a.epochs - done) * (el / max(done, 1))
                print(f"\r  epoch {ep}/{a.epochs}  step {seen}/{steps}  "
                      f"loss {run/seen:.5f}  elapsed {el/60:.0f}m  ETA {eta/60:.0f}m   ",
                      end="", flush=True)
        net.eval(); vl = 0.0; vn = 0; mae = 0.0
        with torch.no_grad():
            for w, b, stm, sc, rs in batches(va, a.batch, dev, shuffle=False):
                out = net(w, b, stm)
                target = a.lam * torch.sigmoid(sc / SCALE) + (1 - a.lam) * rs
                vl += ((torch.sigmoid(out / SCALE) - target) ** 2).mean().item()
                mae += (out - sc).abs().mean().item()
                vn += 1
        print(f"\n  epoch {ep}: train {run/max(seen,1):.5f}  val {vl/max(vn,1):.5f}  "
              f"val MAE vs search score {mae/max(vn,1):.1f} cp")
        torch.save({"model": net.state_dict(), "epoch": ep, "scale": SCALE}, a.out)
    print(f"saved {a.out}")


if __name__ == "__main__":
    main()
