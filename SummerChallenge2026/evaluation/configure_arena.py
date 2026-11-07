"""Recreate the original assessment: URL [PATH_TO_PRE_FIX_SOURCE].

For the updated main.cpp, use evaluate_improvement.py instead.
"""
import hashlib
import json
from pathlib import Path
import shlex
import sys
import urllib.request
import urllib.error

ROOT = Path(__file__).resolve().parents[1]
BASE = sys.argv[1].rstrip("/")


def request(method, path, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(BASE + "/api/" + path, data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        print(error.read().decode(), file=sys.stderr)
        raise


def submit(body):
    for bot in request("GET", "status")["bots"]:
        if bot["name"] == body["name"]:
            existing = request("GET", f"bots/{bot['id']}/source")
            assert existing["source_code"] == body["source_code"]
            return bot
    return request("POST", "bots", body)


if __name__ == "__main__":
    python = shlex.quote(sys.executable)
    adapter = shlex.quote(str(ROOT / "evaluation/referee_adapter.py"))
    launcher = shlex.quote(str(ROOT / "evaluation/bot_launcher.py"))
    config = {
        "game": {"min_players": 2, "max_players": 2, "symmetric": False},
        "evaluation": {"enabled_on_start": True, "seed_sequence_key": 20260908,
                       "stages": [{"name": "Bronze real-referee paired seeds",
                                   "seed_source": {"type": "generated"},
                                   "coverage": {"type": "per_benchmark", "target": 100}}]},
        "ranking": {"algorithm": "BradleyTerry"},
        "workers": [{"type": "embedded", "threads": 2,
                     "cmd_build": f"{python} {launcher} build {{DIR}} {{LANG}}",
                     "cmd_run": f"{python} {launcher} run {{DIR}} {{LANG}}",
                     "referee": {"type": "command",
                                 "play_match": f"{python} {adapter} play {{SEED}} {{REPLAY_PATH}} {{PLAYERS}}",
                                 "watch_replay": f"{python} {adapter} watch {{REPLAY_PATH}} {{REPLAY_DIR}} {{PORT}} {{PLAYER_COUNT}}"}}],
    }
    source_path = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "main.cpp"
    source = source_path.read_text()
    # This diagnostic benchmark differs only by the town-region eligibility check.
    needle = "if (region.inked || hits <= 0 || hits > remaining || protectedRegions[r])"
    replacement = "if (region.inked || hits <= 0 || hits > remaining || protectedRegions[r] ||\n" \
        "                any_of(region.cells.begin(), region.cells.end(), [&](int c) { return tiles[c].town; }))"
    if source.count(needle) != 1:
        raise SystemExit("Original assessment requires the pre-fix source as a second argument. "
                         "Use evaluate_improvement.py for the updated bot.")
    town_safe = source.replace(needle, replacement)
    print(json.dumps(request("PUT", "configuration", config), indent=2))
    benchmark = submit({"name": "Diagnostic: town-safe copy", "language": "cpp",
                                         "source_code": town_safe, "role": "benchmark"})
    reference = submit({"name": "Author Wood1 builder",
                                         "language": "python", "role": "benchmark",
                                         "source_code": (ROOT / "arena_tools/referee/config/level2/Boss.py").read_text()})
    current = submit({"name": "Current Bronze bot (unchanged)", "language": "cpp",
                                       "source_code": source, "role": "candidate"})
    manifest = {"current": current["id"], "town_safe": benchmark["id"],
                "author_builder": reference["id"],
                "source_sha256": hashlib.sha256(source.encode()).hexdigest(), "url": BASE}
    (ROOT / "arena/evaluation_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))
