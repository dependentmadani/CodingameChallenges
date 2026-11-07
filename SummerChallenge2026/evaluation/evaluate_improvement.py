"""Evaluate main.cpp against immutable submissions from the original assessment.

Usage: python3 evaluation/evaluate_improvement.py URL NAME [MATCHES_PER_BENCHMARK]
Run against the separately initialized arena_improvement workspace.
"""
import hashlib
import json
from pathlib import Path
import shlex
import sqlite3
import sys

from configure_arena import request, submit

ROOT = Path(__file__).resolve().parents[1]


def main():
    name = sys.argv[2]
    target = int(sys.argv[3]) if len(sys.argv) > 3 else 100
    assert target > 0 and target % 2 == 0
    python = shlex.quote(sys.executable)
    adapter = shlex.quote(str(ROOT / "evaluation/referee_adapter.py"))
    launcher = shlex.quote(str(ROOT / "evaluation/bot_launcher.py"))
    config = {
        "game": {"min_players": 2, "max_players": 2, "symmetric": False},
        "evaluation": {"enabled_on_start": True, "seed_sequence_key": 20260909,
                       "stages": [{"name": "Bronze improvement paired seeds",
                                   "seed_source": {"type": "generated"},
                                   "coverage": {"type": "per_benchmark", "target": target}}]},
        "ranking": {"algorithm": "BradleyTerry"},
        "workers": [{"type": "embedded", "threads": 2,
                     "cmd_build": f"{python} {launcher} build {{DIR}} {{LANG}}",
                     "cmd_run": f"{python} {launcher} run {{DIR}} {{LANG}}",
                     "referee": {"type": "command",
                                 "play_match": f"{python} {adapter} play {{SEED}} {{REPLAY_PATH}} {{PLAYERS}}",
                                 "watch_replay": f"{python} {adapter} watch {{REPLAY_PATH}} {{REPLAY_DIR}} {{PORT}} {{PLAYER_COUNT}}"}}],
    }
    request("PUT", "configuration", config)
    connection = sqlite3.connect(f"file:{ROOT / 'arena/cgarena.db'}?mode=ro", uri=True)
    connection.row_factory = sqlite3.Row
    benchmarks = {}
    for old_id in (1, 2, 3):
        old = connection.execute("SELECT * FROM bots WHERE id=?", (old_id,)).fetchone()
        benchmark = submit({"name": old["name"], "source_code": old["source_code"],
                            "language": old["language"], "role": "benchmark"})
        benchmarks[old["name"]] = benchmark["id"]
    connection.close()
    source = (ROOT / "main.cpp").read_text()
    candidate = submit({"name": name, "language": "cpp", "role": "candidate", "source_code": source})
    manifest = {"current": candidate["id"], "benchmarks": benchmarks,
                "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
                "url": sys.argv[1], "seed_sequence_key": 20260909}
    arena = ROOT / "arena_improvement"
    payload = json.dumps(manifest, indent=2) + "\n"
    (arena / "evaluation_manifest.json").write_text(payload)
    (arena / f"evaluation_manifest_{candidate['id']}.json").write_text(payload)
    print(payload)


if __name__ == "__main__":
    main()
