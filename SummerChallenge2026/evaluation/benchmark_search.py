"""Compile immutable snapshots, then compare sources on paired referee seeds.

python3 evaluation/benchmark_search.py main.cpp main_bot.cpp --start 1000 --seeds 50
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("opponent", type=Path)
    parser.add_argument("--start", type=int, default=1000)
    parser.add_argument("--seeds", type=int, default=30)
    parser.add_argument("--workers", type=int, choices=(1, 2), default=2)
    args = parser.parse_args()
    if args.seeds < 1:
        parser.error("--seeds must be positive")
    output = Path(tempfile.mkdtemp(prefix="smitsimax-", dir=ROOT / "arena_tools"))
    sources = []
    binaries = []
    for label, source in (("candidate", args.candidate), ("opponent", args.opponent)):
        snapshot = output / f"{label}.cpp"
        shutil.copyfile(source, snapshot)
        executable = output / label
        subprocess.run(["clang++", "-std=c++17", "-O2", "-Wall", "-Wextra",
                        "-Wpedantic", str(snapshot), "-o", str(executable)], check=True)
        sources.append({"file": str(source.resolve()), "snapshot": str(snapshot),
                        "sha256": hashlib.sha256(snapshot.read_bytes()).hexdigest()})
        binaries.append(executable)

    def play(item):
        seed, seat = item
        players = [binaries[1], binaries[1]]
        players[seat] = binaries[0]
        replay = output / f"{seed}-{seat}.json"
        proc = subprocess.run(
            [sys.executable, str(ROOT / "evaluation/referee_adapter.py"), "play",
             str(seed), str(replay), *[shlex.quote(str(p)) for p in players]],
            text=True, capture_output=True, check=True, timeout=150)
        result = json.loads(proc.stdout)
        data = json.loads(replay.read_text())
        rejected = [0, 0]
        for summary in data.get("summaries", []):
            for line in (summary or "").splitlines():
                for player in (0, 1):
                    if ("$" + str(player) + " ") in line and "¤RED¤" in line:
                        rejected[player] += 1
        return {"seed": seed, "seat": seat, "candidate_score": result["scores"][seat],
                "opponent_score": result["scores"][1 - seat],
                "candidate_error": result["errors"][seat],
                "opponent_error": result["errors"][1 - seat],
                "candidate_rejections": rejected[seat],
                "opponent_rejections": rejected[1 - seat], "replay": str(replay)}

    started = time.monotonic()
    items = ((s, p) for s in range(args.start, args.start + args.seeds) for p in (0, 1))
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        matches = []
        for result in pool.map(play, items):
            matches.append(result)
            if len(matches) % 20 == 0:
                print(f"{len(matches)}/{2 * args.seeds} matches", flush=True)
    wins = sum(m["candidate_score"] > m["opponent_score"] for m in matches)
    losses = sum(m["candidate_score"] < m["opponent_score"] for m in matches)
    clean = [m for m in matches if not m["candidate_error"] and not m["opponent_error"]]
    report = {
        "sources": sources, "seed_start": args.start, "seeds": args.seeds,
        "workers": args.workers, "league": 3,
        "referee_commit": subprocess.check_output(
            ["git", "-C", str(ROOT / "arena_tools/referee"), "rev-parse", "HEAD"],
            text=True).strip(),
        "matches": len(matches), "wins": wins, "draws": len(matches) - wins - losses,
        "losses": losses, "candidate_errors": sum(m["candidate_error"] for m in matches),
        "opponent_errors": sum(m["opponent_error"] for m in matches),
        "candidate_rejections": sum(m["candidate_rejections"] for m in matches),
        "clean_wins": sum(m["candidate_score"] > m["opponent_score"] for m in clean),
        "clean_matches": len(clean),
        "mean_margin": sum(m["candidate_score"] - m["opponent_score"] for m in matches) / len(matches),
        "elapsed_seconds": time.monotonic() - started, "results": matches,
    }
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "results"}, indent=2))
    print(f"Report: {output / 'report.json'}")


if __name__ == "__main__":
    main()
