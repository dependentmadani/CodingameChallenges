"""Shrink a C++ bot for CodinGame's source size limit: drops comments, indentation and blank lines.
Keeps preprocessor lines as they are and leaves string/char literals alone.
  python3 arena/minify.py SRC.cpp OUT.cpp"""
import sys

def strip(src):
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c in "\"'":  # literal: copy up to the matching quote
            q, j = c, i + 1
            while j < n and src[j] != q:
                j += 2 if src[j] == "\\" else 1
            out.append(src[i:j + 1]); i = j + 1
        elif src.startswith("//", i):
            while i < n and src[i] != "\n": i += 1
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2); i = n if j < 0 else j + 2
        else:
            out.append(c); i += 1
    return "".join(out)

src = open(sys.argv[1]).read()
lines = []
for l in strip(src).split("\n"):
    s = l.strip()
    if s: lines.append(s)
open(sys.argv[2], "w").write("\n".join(lines) + "\n")
print(f"{len(src)} -> {sum(len(l) + 1 for l in lines)} chars")
