# Middle-strength main_bot.cpp

The upgraded `main_bot.cpp` is stronger than its original build-only version
but remains substantially weaker than `main.cpp` in this sample.

| Opponent | Upgraded main_bot.cpp wins | Draws | Losses |
| --- | ---: | ---: | ---: |
| Original build-only main_bot.cpp | 31 | 0 | 1 |
| main.cpp | 0 | 0 | 32 |

Each comparison used the author referee at Bronze league 3, seeds 0–15 in both
player positions. There were no crashes, timeouts, or other reported bot failures
in the 64 final-version matches. This is a local strength comparison, not a
guarantee of a particular CodinGame ranking.

Changes retained in the middle bot:

- Penalize enemy-owned tracks in its single route search.
- Rank candidate routes by local ownership balance, build time, and instability.
- Disrupt enemy-only regions while protecting towns, the construction route,
  and currently favorable connections.
- Avoid immediately cutting the last possible connection while tied or behind.

It still lacks `main.cpp`'s multiple route searches, whole-board income
simulation, rerouting-aware attacks, and previous-plan preference.

All 15 tests pass in optimized and address/undefined-behavior sanitizer builds.

Final middle-bot SHA-256:
`8323c7f0539b993627835f617beea5309de03f430a52b810b68e3fcadb29929d`.
`main.cpp` was unchanged; its SHA-256 remains
`2a766bf7e36901dead48c9c15acfc39131595180461537bbb646835691ebad55`.

Reports and full replays:

- Versus original basic builder: `arena_tools/simple-bot-check-l6_f6mu0/report.json`.
- Versus main.cpp: `arena_tools/simple-bot-check-8si0issq/report.json`.

The comparison script labels its first executable `strong` and second executable
`simple`. The middle bot was first in the original-builder comparison and second
in the main.cpp comparison.
