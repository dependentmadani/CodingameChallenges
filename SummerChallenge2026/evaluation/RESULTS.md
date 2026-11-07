# Bronze evaluation — 2026-09-08

The current bot reliably beats the author's Wood1 builder, but this evaluation
does **not** establish that it is strong enough for Bronze. Its disruption
selection has a major legality bug.

## Results

CG Arena ran the game author's referee in league 3. Each opponent received
50 deterministic seeds played in both seats: 100 matches per opponent,
200 matches total. Results below are from the unchanged current bot's perspective.

| Opponent | Wins | Draws | Losses | Win rate |
| --- | ---: | ---: | ---: | ---: |
| Author Wood1 builder | 100 | 0 | 0 | 100% |
| Same bot with town-containing regions excluded from attack selection | 21 | 0 | 79 | 21% |

The second opponent differs only by an additional eligibility check, not by a
new strategy. Neither benchmark is an official Bronze boss; the author's
top-level boss file is a WAIT placeholder.

## Main finding

The [author referee rejects disruptions of regions containing towns](https://github.com/CGjupoulton/SummerChallenge2026/blob/2c19cc030266b3bf53efc946dc30c211c6c3eefa/src/main/java/com/codingame/game/Game.java#L552).
The eligibility filter in `main.cpp:209` does not exclude those regions.

Replays recorded **5,585 rejected town-region disruptions** from the current
bot: 5,153 against the diagnostic copy and 432 against the Wood1 builder.
At least one such rejection occurred in 165 of the 200 matches. These are
wasted actions, not crashes or disqualifications.

There were **zero disqualifications or timeouts** for either side. Across 19,280
current-bot decision logs, the self-reported decision time was 2.81 ms at the
95th percentile and 7.17 ms maximum on this machine. This is not a guarantee
of timing on CodinGame hardware.

The earlier synthetic simulator missed town-region immunity, so its perfect
win rate overstated confidence in the bot. The paired real-referee results
make attack legality the first improvement to address.

## Recommended next steps

1. Exclude town-containing regions from disruption candidates in `main.cpp`.
2. Correct the synthetic simulator and add a regression for town immunity.
3. Rerun paired real-referee matches and add stronger opponents before making
   claims about Bronze promotion or ranking.

## Reproducibility and artifacts

- Referee commit: `2c19cc030266b3bf53efc946dc30c211c6c3eefa`.
- Candidate source SHA-256: `ad8be2e1317149cdca81193c3d22e8f144aee0a9307c0d1a240b39c0e1928d8e`.
- `main.cpp` remained unchanged throughout this evaluation.
- Full metrics and example losing replay paths: `arena/evaluation_report.json`.
- Match database and replays: `arena/cgarena.db` and `arena/replays/`.
- Setup and restart instructions: [README.md](README.md).

For one concrete failure, match 2 (seed `-326146809943389536`, current bot in
seat 0) ended 4,504–7,733 with 70 rejected town-region disruptions. Its replay
is `arena/replays/1c63b7ef-7fa3-4fe2-b150-85faf07a7b7d.json`.
