# Search safety and strength validation — 2026-09-17

This report covers the timeout-safe Smitsimax update: ranked eight-action root
portfolios, cached first-ply transitions, bounded opponent preprocessing, a
24-iteration cap, and explicit admission/emergency timing guards. Comparisons
use league 3, both seats per seed, and referee commit
`2c19cc030266b3bf53efc946dc30c211c6c3eefa`.

## Timeout fix

The earlier 48-iteration build was required to finish its fixed workload after
entering search. It measured somewhat stronger than 24 iterations, but produced
sporadic timing forfeits under referee contention. That tradeoff is not suitable
for submission.

The shipped version now:

- admits search only before 20 ms on the clock that includes a 5 ms reserve;
- stops search at 30 ms and returns the precomputed legal heuristic action;
- bounds opponent-route preprocessing and checks the same deadline there; and
- skips the expensive final disruption recomputation when the turn is late.

On seeds **11000–11039**, with two concurrent referee workers to add scheduling
pressure, the fixed bot completed all **80/80 games** with zero candidate errors,
zero opponent errors and zero rejected actions. It scored **43–37** (53.75%)
against `main_bot.cpp`, with mean score margin +73.09.

Artifact: [`report.json`](../arena_tools/smitsimax-0sla8tau/report.json). The
directory contains frozen source copies, binaries and full replays. The tested
candidate snapshot hash is
`aedadd814b7debae701db87a84825dc6ce9e8e3460543f444ad78402b7fa70ed`.

## Rejected higher-budget experiment

An otherwise identical 48-iteration binary scored 28–20 on development seeds
7000–7023 and 25–23 on validation seeds 8000–8023: 53–43 (55.2%) pooled with no
forfeits in those particular runs. Later benchmarks recorded candidate timing
forfeits, including under two-worker load, so the small strength estimate does
not justify shipping it.

No finite local test can prove that every judge run will meet a wall-clock
deadline, but the current bot has both substantially more reserve and a tested
legal fallback instead of blindly completing the search workload.
