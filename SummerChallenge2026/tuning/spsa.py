"""SPSA tuner for main.cpp's weight constants.

Why SPSA and not the coordinate hill-climb used earlier: there are ~53 weights,
and stepping them one at a time spends an evaluation per parameter per pass,
which at these match costs buys only a couple of passes. SPSA perturbs every
parameter at once and needs exactly two evaluations per iteration regardless of
dimension, so the whole vector moves on every step.

Everything is measured against one fixed reference binary, so scores stay
comparable across iterations, and matches are paired by seed (see arena.py).
The exact production timing behavior is retained. `selfcheck.py` must pass on
the host before tuning; otherwise the host cannot evaluate the submitted bot
reliably and the run should stop.

  python3 spsa.py --iters 60 --seeds 24 --workers 8
"""
import argparse
import hashlib
import json
import random
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "main.cpp"
SPEC = {k: v for k, v in json.loads((HERE / "params.json").read_text()).items()
        if not k.startswith("_")}
ALL_NAMES = list(SPEC)


def to_unit(vals, names):
    out = []
    for n in names:
        _, lo, hi, _ = SPEC[n]
        out.append((vals[n] - lo) / (hi - lo))
    return out


def from_unit(vec, names, base):
    vals = dict(base)
    for n, u in zip(names, vec):
        _, lo, hi, kind = SPEC[n]
        x = lo + min(1.0, max(0.0, u)) * (hi - lo)
        vals[n] = int(round(x)) if kind == "i" else round(x, 5)
    return vals


CXX = next((c for c in ("clang++", "g++", "c++") if shutil.which(c)), None)


def build(vals, out, extra=()):
    if CXX is None:
        return False
    flags = [f"-D{n}={v}" for n, v in vals.items()]
    r = subprocess.run([CXX, "-std=c++17", "-O2", *extra, *flags,
                        str(SRC), "-o", str(out)], capture_output=True, text=True)
    return r.returncode == 0


def evaluate(binary, reference, seeds, start, workers):
    r = subprocess.run(
        [sys.executable, str(HERE / "arena.py"), str(binary), str(reference),
         "--seeds", str(seeds), "--start", str(start), "--workers", str(workers),
         "--retry-infra", "2", "--require-clean"],
        capture_output=True, text=True)
    try:
        d = json.loads(r.stdout)
    except Exception:
        return None
    if d.get("failures") or "error" in d or "score" not in d:
        return None
    return d


def in_group(name, group):
    if group == "search":
        return name.startswith("S_") or name == "P_TREE_EXPLORE"
    if group == "attack":
        return name.startswith("A_") or name.startswith("P_ATK_")
    if group == "planner":
        return name.startswith("P_") and not name.startswith("P_ATK_") \
               and name not in ("P_TREE_EXPLORE", "P_TREE_DEPTH")
    return True


def save_result(path, status, parameters, source_hash, args, **metrics):
    payload = {
        "status": status,
        "source_sha256": source_hash,
        "parameters": parameters,
        "settings": vars(args),
        **metrics,
    }
    Path(path).write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iters", type=int, default=60)
    ap.add_argument("--seeds", type=int, default=24)
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--seed-start", type=int, default=1,
                    help="seed 0 excluded: the referee randomises that map")
    ap.add_argument("--validate-seeds", type=int, default=60)
    ap.add_argument("--validate-start", type=int, default=100000)
    ap.add_argument("--test-seeds", type=int, default=100,
                    help="untouched seeds evaluated once after selection")
    ap.add_argument("--test-start", type=int, default=200000)
    ap.add_argument("--every", type=int, default=15, help="validate every N iterations")
    ap.add_argument("--tree-iters", type=int, default=24,
                    help="must match the intended production P_TREE_ITERS")
    ap.add_argument("--group", choices=("search", "attack", "planner", "all"),
                    default="search", help="tune one coherent continuous tier")
    ap.add_argument("--allow-noisy", action="store_true",
                    help="proceed when selfcheck fails; experimental and only defensible "
                         "with a large seed count")
    ap.add_argument("--confidence-z", type=float, default=1.96,
                    help="selection score must clear zero by this many standard errors")
    ap.add_argument("--rng", type=int, default=1)
    ap.add_argument("--out", default=str(HERE / "best.json"))
    args = ap.parse_args()
    if min(args.iters, args.seeds, args.workers, args.validate_seeds,
           args.test_seeds, args.every, args.tree_iters) < 1:
        ap.error("counts, workers and tree iterations must be positive")
    ranges = [
        (args.seed_start, args.seed_start + args.iters * args.seeds, "training"),
        (args.validate_start, args.validate_start + args.validate_seeds, "selection"),
        (args.test_start, args.test_start + args.test_seeds, "final test"),
    ]
    if any(lo <= 0 for lo, _, _ in ranges):
        ap.error("all seed ranges must start above zero")
    for i, (lo, hi, label) in enumerate(ranges):
        for other_lo, other_hi, other_label in ranges[i + 1:]:
            if max(lo, other_lo) < min(hi, other_hi):
                ap.error(f"{label} and {other_label} seed ranges overlap")
    random.seed(args.rng)

    work = HERE / "work"
    work.mkdir(exist_ok=True)
    base = {n: SPEC[n][0] for n in ALL_NAMES}
    # SPSA's continuous perturbation is invalid for rounded structural values:
    # depth 3, for example, rounded to 3 on both sides of the first probe while
    # still receiving a gradient update. Keep integers fixed for discrete sweeps.
    names = [n for n in ALL_NAMES if SPEC[n][3] == "f" and in_group(n, args.group)]
    if not names:
        ap.error("selected group has no continuous parameters")

    extra = (f"-DP_TREE_ITERS={args.tree_iters}",)
    ref = work / "reference"
    if not build(base, ref, extra):
        print("reference build failed", flush=True)
        return 1
    check = subprocess.run([sys.executable, str(HERE / "selfcheck.py"), str(ref),
                            "--workers", str(args.workers),
                            "--tree-iters", str(args.tree_iters)],
                           capture_output=True, text=True)
    print(check.stdout, end="", flush=True)
    if check.returncode:
        if not args.allow_noisy:
            print("selfcheck failed; refusing to tune noisy or timing-unsafe results.\n"
                  "Use the shipped iteration cap, reduce concurrency, or move to a quieter\n"
                  "host. --allow-noisy is experimental and needs hundreds of seed pairs.",
                  flush=True)
            return 1
        # The per-pair sd of the bounded objective measured ~0.58, so resolving a
        # ~3pp win-rate gain at 2 SE needs roughly 580 pairs. Smaller runs mostly
        # move the vector on noise.
        needed = 580
        print(f"selfcheck failed; continuing because --allow-noisy was given.", flush=True)
        if args.seeds < needed:
            print(f"warning: --seeds {args.seeds} is far below the ~{needed} pairs needed to\n"
                  f"         resolve a 3pp effect against this build's run-to-run noise;\n"
                  f"         expect SPSA to follow noise rather than signal.", flush=True)
    source_hash = hashlib.sha256(SRC.read_bytes()).hexdigest()
    print(f"reference built with {CXX}; tuning {len(names)} continuous "
          f"{args.group} parameters; integer parameters fixed", flush=True)

    theta = to_unit(base, names)
    scale = [None]
    # The reference IS the default configuration, so its paired score against
    # itself is 0 by definition. Starting the bar there means a vector is only
    # recorded if it actually beats the config you already ship.
    best_vals, best_score, best_iteration, best_selection = dict(base), 0.0, 0, None
    save_result(args.out, "no_improvement_yet", best_vals, source_hash, args,
                selection=None, final_test=None)
    log = open(HERE / "spsa_log.txt", "a", buffering=1)
    log.write(f"run source={source_hash} group={args.group} rng={args.rng} "
              f"tree_iters={args.tree_iters}\n")
    started = time.time()

    for k in range(1, args.iters + 1):
        # decaying perturbation and step, standard SPSA schedule
        ck = 0.12 / (k ** 0.101)
        ak = 0.10 / ((k + 8) ** 0.602)
        delta = [random.choice((-1.0, 1.0)) for _ in names]

        plus = [t + ck * d for t, d in zip(theta, delta)]
        minus = [t - ck * d for t, d in zip(theta, delta)]
        bp, bm = work / "cand_plus", work / "cand_minus"
        if not (build(from_unit(plus, names, base), bp, extra) and
                build(from_unit(minus, names, base), bm, extra)):
            print(f"iter {k}: build failed", flush=True)
            continue
        # common seed block for both sides of the difference: the perturbation
        # is what should differ between them, not the maps they were judged on
        block = args.seed_start + (k - 1) * args.seeds
        fp = evaluate(bp, ref, args.seeds, block, args.workers)
        fm = evaluate(bm, ref, args.seeds, block, args.workers)
        if fp is None or fm is None:
            print(f"iter {k}: evaluation failed", flush=True)
            continue

        # The raw SPSA gradient is (f+ - f-)/2c. Here f is a score margin in the
        # hundreds and c is ~0.1, so the raw value is ~1e3 while the parameter
        # space is normalised to [0,1] -- applied directly it pins every weight
        # to a bound on the first step. Divide by a running scale of the observed
        # differences and clip, so a step moves a few percent of each range.
        diff = fp["score"] - fm["score"]
        scale[0] = abs(diff) if scale[0] is None else 0.8 * scale[0] + 0.2 * abs(diff)
        g = diff / max(scale[0], 1e-9)
        g = max(-3.0, min(3.0, g))
        theta = [min(1.0, max(0.0, t + ak * g * d)) for t, d in zip(theta, delta)]
        msg = (f"iter {k:3d} f+={fp['score']:+.4f} f-={fm['score']:+.4f} g={g:+.2f} "
               f"step={ak * abs(g):.3f} elapsed={time.time()-started:.0f}s")
        print(msg, flush=True)
        log.write(msg + "\n")

        if k % args.every == 0 or k == args.iters:
            cand = from_unit(theta, names, base)
            bc = work / "cand_validate"
            if not build(cand, bc, extra):
                continue
            result = evaluate(bc, ref, args.validate_seeds, args.validate_start, args.workers)
            score = result["score"] if result else None
            lower = score - args.confidence_z * result["objective_se"] if result else None
            note = f"  select@{k}: objective={score} lower_bound={lower}"
            if result is not None and lower > 0 and score > best_score:
                best_score, best_vals, best_iteration, best_selection = score, cand, k, result
                save_result(args.out, "selected", best_vals, source_hash, args,
                            best_iteration=k, selection=result, final_test=None)
                note += "  <-- new best, saved"
            print(note, flush=True)
            log.write(note + "\n")

    final_result = None
    if best_iteration:
        final_binary = work / "best_final_test"
        if build(best_vals, final_binary, extra):
            final_result = evaluate(final_binary, ref, args.test_seeds,
                                    args.test_start, args.workers)
        save_result(args.out, "tested" if final_result else "final_test_failed",
                    best_vals, source_hash, args, best_iteration=best_iteration,
                    selection=best_selection, final_test=final_result)

    print("\nbest parameters:", json.dumps(best_vals, indent=1), flush=True)
    if best_score <= 0.0:
        print("no candidate beat the default configuration", flush=True)
    print("best selection objective:", best_score, flush=True)
    print("untouched final test:", json.dumps(final_result), flush=True)
    print("flags:", " ".join(f"-D{n}={v}" for n, v in best_vals.items()), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
