"""Real-referee smoke test: FIRST_EXECUTABLE SECOND_EXECUTABLE [SEEDS] [START]."""
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    strong, simple = (str(Path(p).resolve()) for p in sys.argv[1:3])
    seeds = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    start = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    if seeds <= 0:
        raise SystemExit("Seed count must be positive")
    output = Path(tempfile.mkdtemp(prefix="simple-bot-check-", dir=ROOT / "arena_tools"))

    def play(item):
        seed, seat = item
        players = [simple, simple]
        players[seat] = strong
        replay = output / f"seed-{seed}-strong-seat-{seat}.json"
        result = subprocess.run(
            [sys.executable, str(ROOT / "evaluation/referee_adapter.py"), "play",
             str(seed), str(replay), *[shlex.quote(p) for p in players]],
            capture_output=True, text=True, timeout=150, check=True,
        )
        score = json.loads(result.stdout)
        return {"seed": seed, "strong_seat": seat,
                "strong_score": score["scores"][seat],
                "simple_score": score["scores"][1 - seat],
                "errors": score["errors"], "replay": str(replay)}

    with ThreadPoolExecutor(max_workers=2) as pool:
        matches = list(pool.map(play, ((seed, seat) for seed in range(start, start + seeds)
                                       for seat in (0, 1))))
    report = {
        "league": 3, "seed_start": start, "seeds": seeds, "matches": len(matches),
        "strong_wins": sum(m["strong_score"] > m["simple_score"] for m in matches),
        "draws": sum(m["strong_score"] == m["simple_score"] for m in matches),
        "simple_wins": sum(m["strong_score"] < m["simple_score"] for m in matches),
        "bot_errors": sum(sum(m["errors"]) for m in matches), "results": matches,
    }
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "results"}, indent=2))
    print(f"Full report and replays: {output}")


if __name__ == "__main__":
    main()
