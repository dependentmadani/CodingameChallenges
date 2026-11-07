# Smitsimax upgrade snapshot — 2026-09-14

These results apply only to the hashes below. Subsequent tuning-infrastructure
work changed `main.cpp`'s timing defaults and restored `main_bot.cpp` as the
distinct historical opponent; do not attribute this table to the current source
without a new frozen benchmark.

`main.cpp` now uses two independent open-loop action trees rather than a fixed
root payoff table. Its construction planner is retained as a fallback and as a
source of candidate moves. `main_bot.cpp` is unchanged.

## Held-out author-referee results

The final source was frozen before these matches. Seeds **2000–2039** were not
used to select its parameters. Each seed was played in both seats, using league
3 and the unmodified 50 ms / 1000 ms time limits, with two concurrent matches.

| Opponent | Wins | Draws | Losses | Clean games won | Our timeouts | Opponent timeouts |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Previous `main.cpp` | 46 | 0 | 34 | 46/80 (57.5%) | 0 | 0 |
| Unchanged `main_bot.cpp` | 53 | 0 | 27 | 52/78 (66.7%) | 1 | 1 |

There were **no rejected candidate actions** in these 160 matches. The raw win
rate against `main_bot.cpp` was 66.25%. Mean score margins were +31.34 against
the previous main and +433.83 against main_bot; the latter includes timeout
scores, so clean win counts are more useful.

This is a modest observed improvement over our previous bot, not evidence of
90% wins, universal superiority, or guaranteed promotion. The paired games
share maps and are not 160 independent samples of arbitrary opponents.

Our timeout occurred against `main_bot.cpp`, seed 2010, seat 0. The opponent
timed out on seed 2027, with our bot in seat 1. Neither result is silently
discarded: both raw and error-free counts are shown above. Wall-clock timing
and search iteration counts can vary across machines and runs.

The failing seed was then repeated three times in each seat, one match at a
time, with [a timing-only entry point](../tests/timed_bot.cpp) around the unchanged
production code. All six repeats completed without errors; maximum measured
decision time was 18.77 ms. This did not reproduce the timeout, but does not
erase it or prove its cause. Diagnostic replays are retained under
`arena_tools/smitsimax-timing-y30gky/` and are not added to the held-out win rate.

## Source identity and artifacts

- Final `main.cpp`: `bf63313e8e9bb939bd6d75ac5f20f5db51ca15cb6da743e9a8db4ee4eb10e7ef`.
- Previous main: `2a15e505b3b610ece38bfdab3f0c8e7562180d8af94c95ad4fd6cef4e8f62227`.
- `main_bot.cpp`: `8c1c6572309af3bb620fe0823dfcfa8a8c38855214b90f3c0a21b6898bda50ae`.
- Author referee: `2c19cc030266b3bf53efc946dc30c211c6c3eefa`.
- Previous-main report: [report.json](../arena_tools/smitsimax-1mmg2kj1/report.json).
- Main-bot report: [report.json](../arena_tools/smitsimax-_ayspap8/report.json).

The two report directories retain full replays, source copies and compiled
executables locally; `arena_tools/` is git-ignored. The exact previous source
is also preserved in [baselines/main_before_smitsimax.cpp](baselines/main_before_smitsimax.cpp)
so it does not depend on a temporary-directory backup.

## What changed

- Separate UCB trees select both players' policies before applying simultaneous
  moves. Three decision steps and at most 400 iterations fit the time budget.
- Candidate portfolios include alternate construction routes, build orders,
  disruption targets evaluated using whole-board income, and the fallback.
- Future policies re-rank routes and attacks on the simulated board. Opponent
  modelling is mixed with other replies rather than assumed infallible.
- Paired root rollouts compare each action with the baseline under matching
  sampled policy choices; their smaller reward scale has a smaller UCB bonus.
- Scoring is recomputed with N/E/S/W shortest-path ties after simultaneous
  placement, neutral collisions and destruction. Final-turn and total
  disconnection wins/losses are evaluated as terminal results.
- Leaf income is separated into town-protected income and vulnerable income.
  The latter is discounted by the hits needed to destroy its weakest region,
  plus a two-turn allowance. This is a heuristic forecast, not an exact enemy
  attack schedule. It avoids valuing threatened routes as permanently safe.
- The selected construction route and attack, not a discarded proposal, become
  the next turn's remembered plan. Additional preprocessing cutoffs and a
  five-millisecond timing reserve protect the heuristic fallback.

## Development results are not validation results

Depth, candidate, rollout and selection experiments used seeds 200–215 and
1000–1015. An earlier three-turn version won 18/24 development games but then
only 30/64 against the old main on seeds 1000–1031. It won 41/64 against
main_bot on those seeds. That version was not retained.

A four-step greedy continuation did not improve the 1000–1015 result and
introduced two candidate timeouts. Reducing root exploration alone achieved
16/32. Discounting vulnerable income improved that development comparison to
24/32 with no failures. The implementation was then frozen, and evaluated on
the fresh 2000–2039 seeds reported above. The 75% development win rate must not
be presented as the final held-out rate.

## Correctness checks

- Optimized C++17 build: all 15 existing bot test groups passed.
- Address/undefined-behavior sanitizer build: all 15 groups passed.
- Production simulator differential tests: all seven groups passed in optimized
  and ASan/UBSan builds. They include randomized simultaneous turns, neutral
  collisions, shared/directional paths, terminal disconnections, final-turn
  scoring, protected versus vulnerable income, and a 600-cell board with 132
  directed requests. Both player IDs are exercised.
- Synthetic arena: 24/24 against the simple builder and 24/24 against its
  sabotaging variant, with legal actions throughout. These are smoke tests,
  not evidence against a stronger opponent. Response latency including local
  IPC was 28.37 ms at the 95th percentile and 64.66 ms maximum; the latter is
  another reason not to promise universally safe timing from this machine's
  results. It measures a different interval from the decision-only diagnostic.

```sh
clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic main.cpp -o /tmp/summer-smits
python3 test_bot.py /tmp/summer-smits
python3 tests/test_search.py -v
SEARCH_SANITIZE=1 python3 tests/test_search.py -v
python3 test_bot.py /tmp/summer-smits --arena
```

To repeat the reported referee comparisons:

```sh
python3 evaluation/benchmark_search.py main.cpp evaluation/baselines/main_before_smitsimax.cpp --start 2000 --seeds 40
python3 evaluation/benchmark_search.py main.cpp main_bot.cpp --start 2000 --seeds 40
```

For a new validation, use a different seed range, such as `--start 3000`.
Run benchmarks sequentially and avoid other CPU-heavy tasks. `--workers 1`
reduces local scheduling contention. Source hashes and the exact referee
commit are included in every report.
