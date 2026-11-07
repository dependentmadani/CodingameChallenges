"""Build or exec an immutable CG Arena submission."""
import os
from pathlib import Path
import subprocess
import sys

mode, directory, language = sys.argv[1:]
directory = Path(directory).resolve()
source = directory / "source.txt"
executable = directory / "bot"
if mode == "build":
    if language == "cpp":
        subprocess.run(["clang++", "-std=c++17", "-O2", "-x", "c++", str(source),
                        "-o", str(executable)], check=True)
    elif language != "python":
        raise SystemExit(f"Unsupported language: {language}")
elif mode == "run":
    if language == "cpp":
        os.execv(executable, [str(executable)])
    else:
        os.execv(sys.executable, [sys.executable, "-u", str(source)])
