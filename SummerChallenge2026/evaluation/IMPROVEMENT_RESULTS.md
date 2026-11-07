# Tested Bronze improvement — 2026-09-08

The final `main.cpp` fixes disruption eligibility: a region containing any town
is never an attack candidate. Town membership is cached once during initialization.
The existing construction and disruption-scoring strategy is otherwise unchanged.

## Final bot: 300 real-referee matches

The author referee ran in league 3 at commit
`2c19cc030266b3bf53efc946dc30c211c6c3eefa`. Each opponent received 50 generated
seeds in both player positions (100 matches). Seed sequence key `20260909`
differs from the original assessment's `20260908`.

| Opponent | Wins | Draws | Losses |
| --- | ---: | ---: | ---: |
| Previous unchanged bot | 79 | 0 | 21 |
| Author Wood1 builder | 100 | 0 | 0 |
| Original diagnostic town-safe copy | 50 | 0 | 50 |

The previous bot timed out in one losing match. Excluding that match, the final
bot won **78/99 (78.8%)**. It is therefore a clear improvement even without
credit for the opponent failure. The old-bot timeout is match 497, seed
`-3335091813317055479`, old bot in seat 1; its replay tooltip says `Timeout!`.

The diagnostic opponent implements the same strategy with a per-candidate town
scan rather than the cached flag. Its even results and identical mean score
(3,469.06 for both sides) are consistent with that equivalence.

The final candidate had **zero rejected actions, zero crashes, and zero
timeouts** across all 300 matches. Its 27,411 self-reported decision times had
a 2.53 ms 95th percentile and 7.51 ms maximum on this machine. Local timing does
not guarantee identical performance on CodinGame hardware.

## Experiment not retained

A fourth Dijkstra search that favored immune town regions was also tested in
300 matches on the same new seed sequence. It won 86/100 against the original
bot and 100/100 against the Wood1 builder, but only 51/100 against the fix-only
diagnostic. That did not establish an advantage over the simpler fix, so the
extra search was removed. The final `main.cpp` was then separately evaluated
in the 300 matches above; these are not results from the discarded source.

## Regression checks

- The simulator now rejects town-region disruptions and exempts town regions
  from ink-outs. Its synthetic opponent also respects the rule.
- New checks cover enemy tracks sharing a town's region, nonsequential region
  IDs, both player IDs, and boards where all enemy tracks are immune.
- Both attack regressions fail on the old bot and pass on the final bot.
- All 15 test groups pass with an optimized build and with address and
  undefined-behavior sanitizers.
- The final bot wins 24/24 synthetic matches against each of the two simple
  builders (with and without sabotage). These are correctness smoke tests,
  not official ranking evidence.

## Artifacts and limitations

- Final source SHA-256: `328061c9ffdfc341f5d5798dca8f8c8dfcb720e8691818288fb1ea76f2cc50ab`.
- Final candidate ID: 5 in `arena_improvement`.
- Final metrics: `arena_improvement/evaluation_report_5.json`.
- Discarded experiment: candidate 4, `arena_improvement/evaluation_report_4.json`.
- All immutable submissions and 600 new replays remain in `arena_improvement`.
- The original assessment and its 200 replays remain untouched in `arena`.
- [Setup and repeat-evaluation instructions](README.md).

This establishes improvement against the old bot, not a Bronze promotion
guarantee. No official Bronze boss was available in the author's repository;
the Wood1 builder remains a weak comparison opponent. Stronger independent
opponents and held-out seeds are the next useful strength checks.
