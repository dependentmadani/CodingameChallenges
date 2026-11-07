# Summer Challenge 2026 — Smitsimax bot

Submit `main.cpp` using C++17 or newer.

`main_bot.cpp` is an unchanged heuristic opponent for practice matches. Like our
construction planner, it evaluates alternate routes and whole-board income;
it is not a simple nearest-town builder. Compile it separately: both files have
their own `main()`.

```sh
clang++ -std=c++17 -O2 main_bot.cpp -o /tmp/summer-simple-bot
```

For a small head-to-head check using the local author referee:

```sh
clang++ -std=c++17 -O2 main.cpp -o /tmp/summer-strong-bot
python3 evaluation/compare_bots.py /tmp/summer-strong-bot /tmp/summer-simple-bot 8
```

This plays eight seeds in both positions and saves the results and replays in
a new directory under `arena_tools/`.

For the current search-budget results, see
[search-strength evaluation](evaluation/SEARCH_STRENGTH_2026-09-17.md). The last
frozen [Smitsimax evaluation](evaluation/SMITSIMAX_RESULTS.md), the
[middle-bot](evaluation/MIDDLE_BOT_RESULTS.md) and
[improvement](evaluation/IMPROVEMENT_RESULTS.md) reports describe historical
source snapshots; their win rates do not apply to today's files.

## Rules that drive the strategy

`Bronze_challenge.pdf` replaces the Wood objectives with a score contest:
lead after 100 turns, or when all requested connections become impossible.

- Three paint points and one separate disruption point are available each turn.
- Plains, rivers and mountains cost one, two and three paint.
- Only requested town pairs score. Each connection uses its shortest physical
  path, with N/E/S/W priority from the requesting town to break ties.
- Each owned track on each active connection earns its owner one point per turn.
  Shared track can earn points for several connections. Neutral track earns none.
- Four instability permanently inks out a region. Tracks and connections through
  it disappear, and no more tracks can be built there.
- Regions containing a town are immune: they cannot be disrupted or inked out.
- Both players' placements resolve first; coincident placements become neutral.
  Disruption resolves next, and scoring comes last.
- The turn limit is 50 ms, with 1000 ms for the first turn. Boards are 21–30 cells
  wide and 14–20 high, with 4–12 towns.

## Bot strategy

The bot uses both construction and disruption in the same turn when useful.

For construction, five Dijkstra cost functions generate cheap routes, routes
that avoid enemy ownership and instability, short physical shortcuts, fast
completions, and shared trunks. Additional candidates pass through busy waypoints.
Candidates include already connected pairs, allowing the bot to redirect their
active routes through its own tracks.

For each candidate it hypothetically completes the route and recomputes income
for **all** requested connections using the actual shortest-path tie-breaking.
It ranks improvements in our income minus enemy income, discounts slow projects
and instability, and rejects routes that cannot finish before turn 100.
The previous construction route remains a candidate on the following turn.
Placements pack mountains alone, rivers with plains, and remaining plains in
threes. Contested cells are prioritized within each terrain class.

For disruption, it excludes town-containing regions, estimates promising targets,
and evaluates up to 20 by
recomputing income with the entire region removed. This accounts for alternate
paths: destroying an enemy piece does not necessarily eliminate the connection.
It avoids disruptions that worsen our current income advantage, protects the
planned construction route, and can attack unfinished enemy investments when
no active income is lost. It also avoids immediately ending the game through
total disconnection while tied or behind.

The heuristic action seeds a bounded **Smitsimax** search: two independent
open-loop trees, one per player, with three simultaneous decision steps. Both
players choose before either move is applied. Candidate generation considers up
to 18 construction/disruption bundles, including different build orders,
exact-income attack candidates, and the heuristic fallback; a one-turn feature
ranking retains eight for search. Later choices adapt their route and attack
policy to each simulated board.

The simulator resolves neutral collisions, paint budgets, both disruptions,
ink-outs, directional shortest paths, shared income, and game termination.
The leaf evaluation treats town-only income as durable and discounts income
that can disappear during a disruption cycle. This is a forecast, not a promise
that an opponent will attack a particular region.

UCB allocates simulations independently in the two trees. Paired root rollouts
compare candidate actions against the fallback under matching sampled policy
choices. A recursive opponent-perspective predictor remains available behind
`P_PREDICT_FOE`, but is disabled by default because its measured benefit did not
justify its runtime and timeout risk; the opponent tree still receives heuristic
route and disruption candidates.
The bot remembers the route and attack it actually selected, not the discarded
heuristic plan.

Search allows up to 24 iterations over a ranked portfolio of at most eight root
actions, and repeated first-turn transitions are cached. It is admitted only
before 20 ms on the reserved clock and has a hard 30 ms search stop. If either
guard is reached, the bot uses its already-computed legal heuristic action. The
final disruption recomputation is also skipped on a late turn. These reserves
protect the referee's 50 ms limit; timing still varies with hardware.

## Build and check

```sh
clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic main.cpp -o /tmp/summer-bronze-bot
python3 test_bot.py /tmp/summer-bronze-bot
python3 tests/test_search.py -v
python3 test_bot.py /tmp/summer-bronze-bot --arena
```

The independent Python simulator checks simultaneous placement, neutral tracks,
region destruction before scoring, directional path ties, action budgets,
terrain routing, both player IDs, endgame decisions, and input termination.
It rejects invalid actions more strictly than the official referee, which skips
impossible actions.

`tests/test_search.py` differentially checks the production C++ simulator against
the independent Python rules model, including randomized simultaneous turns,
final disconnections, and a 600-cell board with 132 directed requests.
Set `SEARCH_SANITIZE=1` to compile its probe with ASan/UBSan.

For reproducible strength comparisons, compile immutable snapshots and play each
seed in both seats:

```sh
python3 evaluation/benchmark_search.py main.cpp main_bot.cpp --start 3000 --seeds 50
python3 evaluation/benchmark_search.py main.cpp evaluation/baselines/main_before_smitsimax.cpp --start 3000 --seeds 50
```

The runner preserves source hashes, source copies, errors, rejected actions,
scores and full author-referee replays under `arena_tools/smitsimax-*`.

The synthetic opponents and maps are local approximations. The source-to-source
benchmark does use the author's map generator and referee, but does not include
the official Bronze boss and cannot guarantee arena promotion.
The earlier simulator omitted town-region immunity; its old win rates should
not be used as evidence of Bronze strength. See [real-referee evaluation](evaluation/README.md)
for CG Arena results and reproducibility instructions.
