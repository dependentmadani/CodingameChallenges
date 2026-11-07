#!/usr/bin/env python3
"""SPSA tuning of the bot's command-line parameters with the local arena.

Each iteration perturbs every parameter by +/- c_i (random signs), plays theta+ against theta-
on a few seeds with sides swapped, and moves theta toward the winner:
    theta_i += lr * c_i * r * delta_i,   r = (wins+ - wins-) / games  in [-1, 1]

  python3 arena/spsa.py --bot ./bot_v1b --iters 600 --games 4 --jobs 8 --out arena/logs/spsa_v1b

Writes <out>.jsonl (one line per iteration) and <out>.theta.json (current parameters).
Resumes from <out>.theta.json when it exists.
"""
import argparse, json, os, random, re, subprocess, sys, time
from pathlib import Path

# name: (default, perturbation c, low, high)
PARAMS = {
    "tri": (10.0, 1.5, 0, 50), "unit": (4.0, 0.6, 0, 30), "pot": (3.0, 0.5, 0, 20), "gamma": (0.5, 0.08, 0.05, 0.95),
    "threat": (5.0, 0.8, 0, 30), "front": (0.05, 0.02, 0, 1), "predw": (1.0, 0.15, 0, 3), "predfrac": (0.4, 0.06, 0.05, 0.8),
    "atkcost": (8.0, 1.2, 0, 40), "cycle": (5.0, 0.8, 0, 30), "block": (0.0, 0.5, 0, 10), "cycleunsafe": (1.0, 0.15, 0, 1),
    "cyclelose": (0.0, 0.1, 0, 1), "cycletie": (0.5, 0.1, 0, 1), "rws": (0.5, 0.1, 0, 3), "rwp": (1.0, 0.15, 0, 3),
    "rwb": (1.0, 0.15, 0, 3), "rwa": (0.5, 0.1, 0, 3), "multi": (0.3, 0.06, 0, 0.9), "deepsplit": (0.6, 0.08, 0.1, 0.95),
    "pts": (1.0, 0.15, 0, 5), "dtri": (2.0, 0.3, 0, 10), "mix": (0.5, 0.08, 0, 1), "safe": (0.0, 0.3, 0, 10),
    "node": (0.0, 0.3, 0, 10), "inf": (0.0, 0.3, 0, 10), "deny": (0.0, 0.3, 0, 10), "rob": (0.0, 0.1, 0, 1),
    "stack": (0.0, 0.05, 0, 0.5), "guard": (0.5, 0.1, 0, 3), "farm": (0.0, 0.5, 0, 30), "blend": (0.0, 0.5, 0, 10),
}
PATH_PARAMS = {"pathmargin": (15.0, 3.0, 0, 80), "pathsym": (1.0, 0.15, 0, 2), "pathfrac": (0.08, 0.015, 0.02, 0.25)}
LM_PARAMS = {"lw": (1.0, 0.15, 0, 4)}

ap = argparse.ArgumentParser()
ap.add_argument("--bot", required=True)
ap.add_argument("--extra", default="time 17", help="fixed arguments for both sides")
ap.add_argument("--iters", type=int, default=600)
ap.add_argument("--games", type=int, default=4, help="seeds per iteration (2 games each)")
ap.add_argument("--jobs", type=int, default=8)
ap.add_argument("--lr", type=float, default=1.0)
ap.add_argument("--out", required=True)
ap.add_argument("--league", type=int, default=2)
ap.add_argument("--paths", action="store_true", help="also tune the path-action parameters")
ap.add_argument("--only", help="comma list: tune only these parameters")
ap.add_argument("--init", default="", help="comma list name=value: starting values (the bot's current defaults)")
ap.add_argument("--pool", help="file with one opponent command per line: theta+ and theta- each play the same opponents "
                "on the same seeds instead of playing each other")
ap.add_argument("--c", default="", help="comma list name=value: perturbation sizes overriding the defaults")
ap.add_argument("--pool-k", type=int, default=4, help="opponents drawn from the pool per iteration")
args = ap.parse_args()
if args.paths: PARAMS.update(PATH_PARAMS)
PARAMS.update(LM_PARAMS)
if args.only:
    keep = args.only.split(",")
    PARAMS = {k: PARAMS[k] for k in keep}
for kv in filter(None, args.init.split(",")):
    k, v = kv.split("=")
    lo, hi = PARAMS[k][2], PARAMS[k][3]
    PARAMS[k] = (float(v), PARAMS[k][1], lo, hi)

for kv in filter(None, args.c.split(",")):
    k, v = kv.split("=")
    PARAMS[k] = (PARAMS[k][0], float(v), PARAMS[k][2], PARAMS[k][3])

root = Path(__file__).resolve().parent
arena = root / "arena"
theta_file = Path(args.out + ".theta.json")
log_file = Path(args.out + ".jsonl")
theta = {k: v[0] for k, v in PARAMS.items()}
start = 0
if theta_file.exists():
    saved = json.loads(theta_file.read_text())
    theta.update(saved["theta"])
    start = saved["iter"]

def cmd(th):
    return args.bot + " " + args.extra + " " + " ".join(f"{k} {v:.5g}" for k, v in th.items())

pool = [ln.strip() for ln in open(args.pool) if ln.strip() and not ln.startswith("#")] if args.pool else []

def pool_round(plus, minus, seed):
    """theta+ and theta- each play the same pool opponents on the same seeds (both sides), all runs in parallel.
    Returns (r, wins+, 0, wins-) with r = (points+ - points-) / games per theta, in [-1, 1]."""
    opps = rng.sample(pool, min(args.pool_k, len(pool)))
    runs = []
    per = max(1, args.jobs // (2 * len(opps)))
    for o in opps:
        for th in (plus, minus):
            runs.append(subprocess.Popen([str(arena), "--a", cmd(th), "--b", o, "--games", str(args.games), "--seed", str(seed),
                                          "--jobs", str(per), "--time-scale", "3", "--league", str(args.league)],
                                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True))
    pts, games = [0.0, 0.0], [0, 0]
    for i, p in enumerate(runs):
        out = p.communicate()[0]
        m = re.search(r"RESULT games=(\d+) wins=(\d+) draws=(\d+) losses=(\d+)", out)
        if not m:
            print("arena failed:", out[-500:], file=sys.stderr)
            return None
        g, w, d, l = map(int, m.groups())
        pts[i % 2] += w + 0.5 * d
        games[i % 2] += g
    return ((pts[0] - pts[1]) / games[0], int(pts[0]), 0, int(pts[1]))

rng = random.Random(12345 + start)
for it in range(start, args.iters):
    lr = args.lr * (0.3 + 0.7 * (1 - it / max(1, args.iters)))
    delta = {k: rng.choice((-1, 1)) for k in PARAMS}
    plus, minus = {}, {}
    for k, (d, c, lo, hi) in PARAMS.items():
        plus[k] = min(hi, max(lo, theta[k] + c * delta[k]))
        minus[k] = min(hi, max(lo, theta[k] - c * delta[k]))
    seed = rng.randrange(1, 10**9)
    t0 = time.time()
    if pool:
        r = pool_round(plus, minus, seed)
        if r is None:
            time.sleep(5)
            continue
        w, d, l = r[1], r[2], r[3]
        r = r[0]
    else:
        res = subprocess.run([str(arena), "--a", cmd(plus), "--b", cmd(minus), "--games", str(args.games), "--seed", str(seed),
                              "--jobs", str(args.jobs), "--time-scale", "3", "--league", str(args.league)], capture_output=True, text=True)
        m = re.search(r"RESULT games=(\d+) wins=(\d+) draws=(\d+) losses=(\d+)", res.stdout)
        if not m:
            print("arena failed:", res.stdout[-500:], res.stderr[-500:], file=sys.stderr)
            time.sleep(5)
            continue
        g, w, d, l = map(int, m.groups())
        r = (w - l) / g
    for k, (dflt, c, lo, hi) in PARAMS.items():
        theta[k] = min(hi, max(lo, theta[k] + lr * c * r * delta[k]))
    with log_file.open("a") as f:
        f.write(json.dumps({"iter": it, "seed": seed, "w": w, "d": d, "l": l, "r": r, "secs": round(time.time() - t0, 1), "theta": theta}) + "\n")
    theta_file.write_text(json.dumps({"iter": it + 1, "theta": theta}, indent=1))
    print(f"iter {it} r={r:+.2f} ({w}-{d}-{l}) {time.time() - t0:.0f}s", flush=True)
