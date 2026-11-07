#!/usr/bin/env python3
"""Bake an SPSA theta into a bot's default parameters.
  python3 arena/bake.py SRC.cpp THETA.json OUT.cpp"""
import json, re, sys
NAME = {"tri": "P_TRI", "unit": "P_UNIT", "pot": "P_POT", "gamma": "P_GAMMA", "threat": "P_THREAT", "front": "P_FRONT",
        "predw": "PRED_W", "predfrac": "PRED_FRAC", "atkcost": "P_ATK", "cycle": "P_CYCLE", "block": "P_BLOCK",
        "cycleunsafe": "P_CYCLE_UNSAFE", "cyclelose": "P_CYCLE_LOSE", "cycletie": "P_CYCLE_TIE", "rws": "RW_STATIC",
        "rwp": "RW_PRED", "rwb": "RW_BR", "rwa": "RW_ADV", "multi": "P_MULTI", "deepsplit": "DEEP_SPLIT", "pts": "P_PTS",
        "dtri": "P_DTRI", "mix": "DEEP_MIX", "safe": "P_SAFE", "node": "P_NODE", "inf": "P_INF", "deny": "P_DENY",
        "rob": "P_ROB", "stack": "P_STACK", "guard": "P_GUARD",
        "pathmargin": "PATH_MARGIN", "pathsym": "PATH_SYM", "pathfrac": "PATH_FRAC"}
src, th, out = sys.argv[1:4]
code = open(src).read()
theta = json.load(open(th))["theta"]
for k, v in theta.items():
    var = NAME[k]
    code, n = re.subn(r"(\b" + var + r" = )[-0-9.e]+", lambda m: m.group(1) + f"{v:.5g}", code, count=1)
    if n != 1: sys.exit(f"{var} not found")
open(out, "w").write(code)
