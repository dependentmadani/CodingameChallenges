"""CG Arena command adapter for the author's referee, explicitly at Bronze (3)."""
import html
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
JAR = ROOT / "arena_tools/referee/target/referee.jar"
JAVA = "/Library/Java/JavaVirtualMachines/jdk-26.jdk/Contents/Home/bin/java"


def play(seed, replay_path, commands):
    args = [JAVA, "--add-opens", "java.base/java.lang=ALL-UNNAMED", "-jar", str(JAR),
            "-league", "3", "-seed", seed, "-l", str(Path(replay_path).resolve())]
    for index, command in enumerate(commands, 1):
        args += [f"-p{index}", command]
    result = subprocess.run(args, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(result.stderr[-12000:])
    replay = json.loads(Path(replay_path).read_text())
    scores = [replay["scores"][str(i)] for i in range(len(commands))]
    attributes = []
    summaries = "\n".join(line or "" for line in replay.get("summaries", []))
    for player, score in enumerate(scores):
        attributes.append({"name": "score", "player": player, "value": str(score)})
        invalid_towns = len(re.findall(rf"\${player} Cannot disrupt region\d+\. It contains a town", summaries))
        attributes.append({"name": "invalid_town_disruptions", "player": player,
                           "value": str(invalid_towns)})
        logs = "\n".join(line or "" for line in replay.get("errors", {}).get(str(player), []))
        times = [float(v) for v in re.findall(r"decision_ms=([\d.]+)", logs)]
        if times:
            attributes.append({"name": "decision_max_ms", "player": player,
                               "value": str(max(times))})
    print(json.dumps({"scores": scores,
                      "ranks": [sum(other > score for other in scores) for score in scores],
                      "errors": [int(score < 0) for score in scores],
                      "attributes": attributes}))


def watch(replay_path, replay_dir):
    # A portable textual replay view; the original full JSON is retained by CG Arena.
    replay = json.loads(Path(replay_path).read_text())
    target = Path(replay_dir)
    target.mkdir(parents=True, exist_ok=True)
    text = html.escape(json.dumps(replay, indent=2))
    (target / "test.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Bronze referee replay</title>'
        '<h1>Bronze referee replay JSON</h1><pre style="white-space:pre-wrap">' + text + '</pre>')


if __name__ == "__main__":
    if sys.argv[1] == "play":
        play(sys.argv[2], sys.argv[3], sys.argv[4:])
    elif sys.argv[1] == "watch":
        watch(sys.argv[2], sys.argv[3])
    else:
        raise SystemExit("Expected play or watch")
