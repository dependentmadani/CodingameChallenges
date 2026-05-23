#!/usr/bin/env python3
"""
SC2026 heuristic auto-tuner.

Random-searches over the constants in main.cpp listed in search_space.json:
  - For each trial, samples one value per parameter
  - Regex-patches main.cpp with the chosen values
  - Compiles main.cpp -> /tmp/bot_under_test
  - Runs N arena games against pre-built /tmp/sc_bot_rival
  - Records mean delta, win rate, per-game results

Outputs (under TUNER_RESULTS env or ./results):
  - runs.jsonl: one line per trial (config + results)
  - best.json:  current best config seen so far
  - progress.log: human-readable progress

Resumable: re-running picks up where it left off (skips trials whose
random seed already appears in runs.jsonl).
"""

import json
import os
import random
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

# ---- paths ----
SCRIPT_DIR  = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
MAIN_CPP    = PROJECT_DIR / "main.cpp"
RIVAL_CPP   = PROJECT_DIR / "other_code.cpp"
ARENA_CPP   = PROJECT_DIR / "arena.cpp"
RESULTS_DIR = Path(os.environ.get("TUNER_RESULTS", SCRIPT_DIR / "results"))
RESULTS_DIR.mkdir(parents=True, exist_ok=True)
RUNS_JSONL  = RESULTS_DIR / "runs.jsonl"
BEST_JSON   = RESULTS_DIR / "best.json"
PROGRESS    = RESULTS_DIR / "progress.log"
TMP_DIR     = Path("/tmp")
BOT_PATH    = TMP_DIR / "bot_under_test"
RIVAL_PATH  = TMP_DIR / "sc_bot_rival"
ARENA_PATH  = TMP_DIR / "sc_arena"


def log(msg: str) -> None:
    ts = time.strftime("%H:%M:%S")
    line = f"[{ts}] {msg}"
    print(line, flush=True)
    with PROGRESS.open("a") as f:
        f.write(line + "\n")


# ---- main.cpp patching ----
def patch_param(content: str, old_value_regex: str, new_value) -> str:
    """Replace the value captured by group 1 of `old_value_regex` with `new_value`.

    The full match (group 0) is rewritten by substituting the old value
    inside it with the new value, ensuring surrounding text is preserved
    exactly. Only the first occurrence is replaced (we assume each
    anchor pattern is unique in the file).
    """
    pattern = re.compile(old_value_regex)
    match = pattern.search(content)
    if not match:
        raise ValueError(f"Pattern not found: {old_value_regex}")
    old_value = match.group(1)
    old_full  = match.group(0)
    new_full  = old_full.replace(old_value, str(new_value), 1)
    return content.replace(old_full, new_full, 1)


def apply_config(template: str, params: dict, config: dict) -> str:
    """Apply a sampled config to the main.cpp template."""
    content = template
    for name, value in config.items():
        old_regex = params[name]["old_value_regex"]
        content = patch_param(content, old_regex, value)
    return content


# ---- compilation + arena ----
def compile_bot(src_path: Path, bin_path: Path, timeout: int = 60) -> tuple[bool, str]:
    cmd = ["g++", "-std=c++17", "-O2", "-o", str(bin_path), str(src_path)]
    try:
        proc = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout
        )
    except subprocess.TimeoutExpired:
        return False, "compile timeout"
    if proc.returncode != 0:
        return False, proc.stderr[:2000]
    return True, ""


def run_one_game(map_id: int = 0, inv_id: int = 0, timeout: int = 30) -> tuple[bool, int]:
    """Run a single arena game on the given map/inv combo. Returns
    (ok, delta_us_minus_rival)."""
    cmd = [str(ARENA_PATH), str(BOT_PATH), str(RIVAL_PATH), "300",
           "--map", str(map_id), "--inv", str(inv_id)]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, 0
    if proc.returncode != 0:
        return False, 0
    m = re.search(r"^delta:\s*(-?\d+)", proc.stdout, re.MULTILINE)
    if not m:
        return False, 0
    return True, int(m.group(1))


# Multi-map game generator. Cycles through (map, inv) combos so each
# trial sees diverse starting conditions — reduces overfitting to the
# specific image_3 layout. 8 combos × 2 games = 16 games per trial.
MAP_INV_COMBOS = [
    (0, 0), (0, 1), (0, 2), (0, 3),  # original map, 4 inv variants
    (1, 0), (1, 1), (1, 2), (1, 3),  # mirrored map, 4 inv variants
]

def run_trial_games(n_total: int, timeout: int) -> tuple[list[int], list[dict]]:
    """Distribute n_total games across the map/inv combos. Returns
    (deltas, per_game_meta) where per_game_meta logs which combo each
    delta came from so we can spot map-specific weaknesses post-hoc."""
    deltas: list[int] = []
    meta: list[dict] = []
    n_combos = len(MAP_INV_COMBOS)
    games_per_combo = max(1, n_total // n_combos)
    # Run round-robin across combos
    for combo_idx, (map_id, inv_id) in enumerate(MAP_INV_COMBOS):
        for _ in range(games_per_combo):
            ok, d = run_one_game(map_id=map_id, inv_id=inv_id, timeout=timeout)
            if ok:
                deltas.append(d)
                meta.append({"map": map_id, "inv": inv_id, "delta": d})
            if len(deltas) >= n_total:
                return deltas, meta
    return deltas, meta


# ---- search ----
def sample_config(rng: random.Random, params: dict) -> dict:
    return {name: rng.choice(meta["values"]) for name, meta in params.items()}


def trial_id(config: dict) -> str:
    return json.dumps(config, sort_keys=True)


def load_completed() -> set[str]:
    if not RUNS_JSONL.exists():
        return set()
    done = set()
    with RUNS_JSONL.open() as f:
        for line in f:
            try:
                row = json.loads(line)
                done.add(trial_id(row["config"]))
            except (json.JSONDecodeError, KeyError):
                continue
    return done


def main() -> int:
    log(f"tuner start, project={PROJECT_DIR}, results={RESULTS_DIR}")

    space_path = SCRIPT_DIR / "search_space.json"
    space = json.loads(space_path.read_text())
    params = space["parameters"]
    cfg = space["tuning"]

    # CLI / env-var overrides for smoke / arena calibration runs.
    if "--trials" in sys.argv:
        cfg["trials"] = int(sys.argv[sys.argv.index("--trials") + 1])
    if "--games" in sys.argv:
        cfg["games_per_trial"] = int(sys.argv[sys.argv.index("--games") + 1])
    if env_trials := os.environ.get("TUNER_TRIALS"):
        cfg["trials"] = int(env_trials)
    if env_games := os.environ.get("TUNER_GAMES"):
        cfg["games_per_trial"] = int(env_games)

    rng = random.Random(cfg.get("random_seed", 42))
    template = MAIN_CPP.read_text()

    # Pre-compile rival + arena once. They don't change across trials.
    log("compiling rival...")
    ok, err = compile_bot(RIVAL_CPP, RIVAL_PATH)
    if not ok:
        log(f"rival compile FAILED: {err[:300]}")
        return 1

    log("compiling arena...")
    ok, err = compile_bot(ARENA_CPP, ARENA_PATH)
    if not ok:
        log(f"arena compile FAILED: {err[:300]}")
        return 1

    completed = load_completed()
    log(f"resuming with {len(completed)} completed trials in jsonl")

    best_score = -1e9
    best_row = None
    # Load existing best if present
    if BEST_JSON.exists():
        try:
            best_row = json.loads(BEST_JSON.read_text())
            best_score = best_row["score"]
            log(f"loaded prior best score={best_score:.2f}")
        except (json.JSONDecodeError, KeyError):
            pass

    trials_target = cfg["trials"]
    games_per     = cfg["games_per_trial"]
    compile_to    = cfg["compile_timeout_sec"]
    game_to       = cfg["game_timeout_sec"]

    started = len(completed)
    for trial in range(started, trials_target):
        # Sample until we get a new config (cheap; space is large enough).
        for _ in range(50):
            config = sample_config(rng, params)
            if trial_id(config) not in completed:
                break
        else:
            log("could not sample novel config; exiting")
            break

        log(f"--- trial {trial+1}/{trials_target} ---")
        log(f"config: {config}")

        # Patch + compile
        try:
            patched = apply_config(template, params, config)
        except ValueError as e:
            log(f"PATCH FAILED: {e}")
            continue

        # Write to a temp source file and compile
        tmp_src = TMP_DIR / "bot_under_test.cpp"
        tmp_src.write_text(patched)
        t0 = time.time()
        ok, err = compile_bot(tmp_src, BOT_PATH, timeout=compile_to)
        if not ok:
            log(f"compile FAILED: {err[:300]}")
            row = {"trial": trial+1, "config": config, "error": "compile_failed",
                   "compile_stderr": err[:500]}
            with RUNS_JSONL.open("a") as f:
                f.write(json.dumps(row) + "\n")
            completed.add(trial_id(config))
            continue
        compile_time = time.time() - t0

        # Run games (multi-map: cycles through map/inv combos)
        t1 = time.time()
        deltas, meta = run_trial_games(games_per, timeout=game_to)
        game_time = time.time() - t1

        if not deltas:
            log("no games completed (timeout?)")
            row = {"trial": trial+1, "config": config, "error": "all_games_failed"}
            with RUNS_JSONL.open("a") as f:
                f.write(json.dumps(row) + "\n")
            completed.add(trial_id(config))
            continue

        wins   = sum(1 for d in deltas if d > 0)
        losses = sum(1 for d in deltas if d < 0)
        ties   = sum(1 for d in deltas if d == 0)
        mean   = statistics.mean(deltas)
        stdev  = statistics.stdev(deltas) if len(deltas) > 1 else 0.0

        # Score: mean delta + bonus for wins. A config that wins on
        # multiple maps gets a stronger signal than one that wins only
        # on the easiest combo, so the bonus stays proportional to
        # total wins across diverse conditions.
        score = mean + 20.0 * wins

        row = {
            "trial":         trial + 1,
            "config":        config,
            "deltas":        deltas,
            "per_game":      meta,
            "wins":          wins,
            "losses":        losses,
            "ties":          ties,
            "mean":          round(mean, 2),
            "stdev":         round(stdev, 2),
            "score":         round(score, 2),
            "compile_sec":   round(compile_time, 2),
            "games_sec":     round(game_time, 2),
        }
        with RUNS_JSONL.open("a") as f:
            f.write(json.dumps(row) + "\n")
        completed.add(trial_id(config))

        log(f"result: deltas={deltas} wins={wins} mean={mean:.1f} score={score:.1f}")

        if score > best_score:
            best_score = score
            best_row = row
            BEST_JSON.write_text(json.dumps(row, indent=2))
            log(f"★ new best (score={score:.1f}, wins={wins}/{games_per})")

    log(f"tuner done. best score={best_score:.2f}")
    if best_row:
        log(f"best config: {best_row['config']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
