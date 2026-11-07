"""Referee wrapper, portable version of evaluation/referee_adapter.py.

The original hardcodes a macOS JDK path; here the JVM and jar locations come
from the environment so the same bundle runs on a Linux cloud box.

  REFEREE_JAR  path to referee.jar   (default: ../arena_tools/referee/target/referee.jar)
  JAVA_BIN     java executable       (default: java on PATH)
"""
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
JAR = Path(os.environ.get("REFEREE_JAR",
                          HERE.parent / "arena_tools/referee/target/referee.jar"))
JAVA = os.environ.get("JAVA_BIN", "java")


def play(seed, replay_path, commands):
    args = [JAVA, "--add-opens", "java.base/java.lang=ALL-UNNAMED", "-jar", str(JAR),
            "-league", "3", "-seed", str(seed), "-l", str(Path(replay_path).resolve())]
    for index, command in enumerate(commands, 1):
        args += [f"-p{index}", command]
    result = subprocess.run(args, capture_output=True, text=True, timeout=300)
    if result.returncode:
        raise RuntimeError(result.stderr[-4000:])
    replay = json.loads(Path(replay_path).read_text())
    scores = [replay["scores"][str(i)] for i in range(len(commands))]
    print(json.dumps({"scores": scores,
                      "errors": [int(score < 0) for score in scores]}))


if __name__ == "__main__":
    if sys.argv[1] != "play":
        raise SystemExit("Expected: play SEED REPLAY BOT_A BOT_B")
    play(sys.argv[2], sys.argv[3], sys.argv[4:])
