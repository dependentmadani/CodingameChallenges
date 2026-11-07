"""Paired match runner for tuning and validation.

Every seed is played in both seats so seat bias cancels. The optimization score
is a per-seed bounded utility dominated by match outcome; raw paired margin and
win rate are also reported. Pairing matters because map variance can otherwise
drown the parameter signal.

Infrastructure failures may be retried. A bot timeout is never retried: it can
depend on the parameter vector and therefore cannot safely be called noise.
"""
import argparse
import json
import math
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
ADAPTER = HERE / "referee_adapter.py"


def one(adapter, seed, seat, a, b, retries):
    players = [b, b]
    players[seat] = a
    for _ in range(retries + 1):
        with tempfile.NamedTemporaryFile(suffix=".json") as tmp:
            try:
                out = subprocess.run(
                    [sys.executable, str(adapter), "play", str(seed), tmp.name, *players],
                    capture_output=True, text=True, timeout=300, check=True)
                scores = json.loads(out.stdout)["scores"]
            except Exception:
                continue
        sa, sb = scores[seat], scores[1 - seat]
        if sa >= 0 and sb >= 0:
            return sa - sb, False
        # A forfeit is a real bot result. Retrying it selectively would favor
        # expensive configurations that only finish on their lucky runs.
        return (-1 if sa < 0 else 1) * 10 ** 6, True
    return None, True


def compare(a, b, seeds, start, workers, retries, require_clean,
            outcome_weight, margin_scale):
    jobs = [(s, seat) for s in range(start, start + seeds) for seat in (0, 1)]
    with ThreadPoolExecutor(max_workers=workers) as pool:
        raw = list(pool.map(lambda j: one(ADAPTER, j[0], j[1], a, b, retries), jobs))

    deltas, bad = [], 0
    for (seed, seat), (d, failed) in zip(jobs, raw):
        if d is None:
            bad += 1
            continue
        if failed:
            bad += 1
        deltas.append((seed, seat, d))

    if require_clean and bad:
        return {"error": "incomplete or forfeited match", "failures": bad,
                "matches": len(deltas)}

    # Pair the two seats of a seed into one observation. The optimization target
    # follows match wins, with a small bounded margin term to give SPSA a smoother
    # signal without allowing one blowout to outweigh many losses.
    byseed = {}
    for seed, seat, d in deltas:
        byseed.setdefault(seed, []).append(d)
    paired_margin = [sum(v) / len(v) for v in byseed.values() if len(v) == 2]
    utility = lambda d: outcome_weight * ((d > 0) - (d < 0)) + \
                        (1.0 - outcome_weight) * math.tanh(d / margin_scale)
    paired = [sum(utility(d) for d in v) / 2.0
              for v in byseed.values() if len(v) == 2]

    n = len(paired)
    if n < 2:
        return {"error": "not enough completed pairs", "pairs": n, "failures": bad}
    mean = sum(paired) / n
    raw_mean = sum(paired_margin) / n
    sd = (sum((x - mean) ** 2 for x in paired) / (n - 1)) ** 0.5
    se = sd / math.sqrt(n)
    wins = sum(1 for _, _, d in deltas if d > 0)
    draws = sum(1 for _, _, d in deltas if d == 0)
    total = len(deltas)
    return {
        "pairs": n, "matches": total, "failures": bad,
        "winrate": round(wins / total, 4) if total else 0,
        "wins": wins, "draws": draws, "losses": total - wins - draws,
        "paired_mean": round(raw_mean, 1),
        "objective": round(mean, 6),
        "objective_se": round(se, 6),
        "paired_t": round(mean / se, 2) if se else 0.0,
        "score": round(mean, 4),
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument("bot_a")
    p.add_argument("bot_b")
    p.add_argument("--seeds", type=int, default=40)
    p.add_argument("--start", type=int, default=1,
                   help="seed 0 is excluded by default: the referee randomises it")
    p.add_argument("--workers", type=int, default=4)
    p.add_argument("--retry-infra", type=int, default=0,
                   help="retry adapter/process failures; bot forfeits are never retried")
    p.add_argument("--require-clean", action="store_true",
                   help="return no score if any match fails or forfeits")
    p.add_argument("--outcome-weight", type=float, default=0.8)
    p.add_argument("--margin-scale", type=float, default=500.0)
    args = p.parse_args()
    if args.start == 0:
        print("warning: seed 0 produces a different map every run; results will be noisy",
              file=sys.stderr)
    if not 0.0 <= args.outcome_weight <= 1.0:
        p.error("--outcome-weight must be in [0, 1]")
    if args.margin_scale <= 0 or args.seeds < 1 or args.workers < 1 or args.retry_infra < 0:
        p.error("seeds, workers and margin scale must be positive; retries cannot be negative")
    res = compare(str(Path(args.bot_a).resolve()), str(Path(args.bot_b).resolve()),
                  args.seeds, args.start, args.workers, args.retry_infra,
                  args.require_clean, args.outcome_weight, args.margin_scale)
    print(json.dumps(res, indent=2))


if __name__ == "__main__":
    main()
