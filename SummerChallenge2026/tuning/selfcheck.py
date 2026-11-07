"""Confirm this machine can produce trustworthy tuning results.

SPSA reads a difference between two configurations, so anything that makes one
configuration score differently against itself is indistinguishable from signal.
The bot keeps the production wall-clock gates. This check verifies that the
chosen host, concurrency and work cap nevertheless produce repeatable actions
and scores before committing hours of compute.

  python3 selfcheck.py            # builds the reference itself
  python3 selfcheck.py ./mybot
"""
import json
import hashlib
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "main.cpp"
SPEC = {k: v for k, v in json.loads((HERE / "params.json").read_text()).items()
        if not k.startswith("_")}


CXX = next((c for c in ("clang++", "g++", "c++") if shutil.which(c)), None)


def build(out, iters):
    if CXX is None:
        print("no C++ compiler found (looked for clang++, g++, c++)")
        return False
    flags = [f"-D{n}={v[0]}" for n, v in SPEC.items()]
    r = subprocess.run([CXX, "-std=c++17", "-O2",
                        f"-DP_TREE_ITERS={iters}", *flags, str(SRC), "-o", str(out)],
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-1500:])
        return False
    return True


def run(bot, seed):
    with tempfile.NamedTemporaryFile(suffix=".json") as t:
        r = subprocess.run([sys.executable, str(HERE / "referee_adapter.py"),
                            "play", str(seed), t.name, str(bot), str(bot)],
                           capture_output=True, text=True)
        if r.returncode:
            return None
        result = json.loads(r.stdout)
        replay = json.loads(Path(t.name).read_text())
        fingerprint = hashlib.sha256(json.dumps({
            "scores": result["scores"],
            "outputs": replay.get("outputs", {}),
        }, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        return result["scores"], fingerprint


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("bot", nargs="?")
    parser.add_argument("--workers", type=int, default=1,
                        help="use the same concurrency planned for tuning")
    parser.add_argument("--tree-iters", type=int,
                        default=int(os.environ.get("TREE_ITERS", "24")))
    args = parser.parse_args()
    if args.workers < 1 or args.tree_iters < 1:
        parser.error("workers and tree iterations must be positive")
    iters = args.tree_iters
    if args.bot:
        bot = Path(args.bot).resolve()
    else:
        bot = HERE / "work" / "selfcheck_bot"
        bot.parent.mkdir(exist_ok=True)
        print(f"building production-equivalent reference with {CXX} "
              f"(-DP_TREE_ITERS={iters}) ...")
        if not build(bot, iters):
            print("BUILD FAILED"); return 1

    # Seed 0 is deliberately excluded: the referee treats it as a sentinel and
    # generates a fresh random map on every run, so it is unreproducible by
    # construction and would fail this check on any machine.
    seeds = [1, 2, 3, 4, 5, 6, 7, 8]
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        results = list(pool.map(lambda job: run(bot, job[0]),
                                [(s, repeat) for s in seeds for repeat in (0, 1)]))
    stable, unstable, timeouts = 0, [], 0
    for i, s in enumerate(seeds):
        a, b = results[2 * i], results[2 * i + 1]
        if a is None or b is None:
            print(f"  seed {s}: referee error"); continue
        if min(a[0]) < 0 or min(b[0]) < 0:
            timeouts += 1
            print(f"  seed {s}: TIMEOUT {a[0]} {b[0]}")
            continue
        if a == b:
            stable += 1
        else:
            unstable.append((s, a, b))
            print(f"  seed {s}: UNSTABLE actions or scores differ")
    print(f"\nstable={stable}/{len(seeds)}  unstable={len(unstable)}  timeouts={timeouts}")
    if stable == len(seeds):
        print("PASS - repeated runs agree; paired deltas will cancel and SPSA can read real gradients.")
        return 0
    print("FAIL - this machine is too busy (or too slow) for reliable tuning.")
    print("Close other CPU users, lower --workers, or lower -DP_TREE_ITERS, then re-run.")
    print("Tuning on an unstable box mostly optimises noise.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
