# Real-referee evaluation

Current two-tree search: [SMITSIMAX_RESULTS.md](SMITSIMAX_RESULTS.md).
Historical 200-match findings: [RESULTS.md](RESULTS.md).
Earlier bot and 600 subsequent matches: [IMPROVEMENT_RESULTS.md](IMPROVEMENT_RESULTS.md).

## Current source-to-source benchmark

```sh
python3 evaluation/benchmark_search.py main.cpp main_bot.cpp --start 3000 --seeds 50
python3 evaluation/benchmark_search.py main.cpp evaluation/baselines/main_before_smitsimax.cpp --start 3000 --seeds 50
```

This compiles immutable copies before starting games, uses both seats for each
seed, and saves SHA-256 hashes, scores, referee errors and rejected-action counts.
The baseline is the exact `main.cpp` saved before replacing its root-only search.
Full source snapshots and replays are retained locally under the git-ignored
`arena_tools/smitsimax-*` directories. Run only one benchmark at a time: competing
CPU-heavy jobs can affect the referee's wall-clock timeouts. Use `--workers 1`
on heavily loaded machines. The adapter requires the local Java/referee setup
described below; it does not download or install dependencies.

## Historical arena setup

This evaluation uses the supplied CG Arena source checkout and the game author's
referee from https://github.com/CGjupoulton/SummerChallenge2026, pinned to commit
`2c19cc030266b3bf53efc946dc30c211c6c3eefa`.

The challenge rules are unmodified. Only the maintained CG Arena CLI and its Maven
packaging configuration were added to the local referee checkout.
The command adapter explicitly selects league **3 (Bronze)** and keeps the
referee's 50 ms / 1000 ms timing limits. The managed CG Arena adapter defaults to
league 19, so this evaluation uses the custom-command integration instead.

## Artifacts

- `../arena/`: SQLite database, immutable submissions, builds, and full JSON replays.
- `../arena_tools/`: local CG Arena build, author referee checkout, Maven and dependencies.
- `referee_adapter.py`: runs the referee and returns CG Arena match results.
- `bot_launcher.py`: builds C++ submissions and launches C++ or Python bots.
- `configure_arena.py`: configures a fresh arena and submits the test cohort.
- `report_arena.py`: reads the recorded matches and writes `arena/evaluation_report.json`.

These tests keep `main.cpp` unchanged. The diagnostic benchmark is the same bot
with one additional disruption-eligibility check: skip regions containing towns.
The other benchmark is the author's **Wood1** builder, not the official Bronze
boss. The repository's top-level `config/Boss.py` is only a WAIT placeholder.

Each benchmark receives 100 matches: 50 deterministic seeds, both player seats.
Two matches run concurrently. No candidate is automatically promoted or rejected.

## Restart and inspect

From the challenge directory:

```sh
arena_tools/target/release/cgarena run arena
python3 evaluation/report_arena.py
```

The first command prints the localhost UI URL. The arena retains the completed
evaluation across restarts. Replay links display the original referee JSON in a
textual viewer; animated replay rendering is not configured here.

To recreate the original assessment in a newly initialized arena only, run
`python3 evaluation/configure_arena.py http://localhost:PORT PATH_TO_PRE_FIX_SOURCE`
with the actual port and an archived pre-fix source file.
The setup script checks existing immutable sources before reusing submissions.

## Referee discrepancy

The referee rejects disruption of any region containing a town (`Game.java`,
the disruption validation and instability checks). The PDF-derived Python
simulator allowed this, so its earlier perfect win rates overstated confidence
in the submitted bot. CG Arena counts a match as error-free when a bot is not
disqualified; skipped impossible actions are counted separately in this report.

## Improvement evaluation

`evaluate_improvement.py` uses a separate `arena_improvement` workspace, leaving
the original assessment intact. It imports the three immutable original
submissions as benchmarks and submits the current `main.cpp` as a new candidate.
It uses seed sequence key `20260909`, different from the original assessment's
`20260908`, and defaults to 100 matches per benchmark (50 seeds, both seats).

```sh
arena_tools/target/release/cgarena run arena_improvement
python3 evaluation/evaluate_improvement.py http://localhost:PORT 'Unique candidate name' 100
python3 evaluation/report_arena.py arena_improvement
```

Use the actual port printed by the first command. Each new source requires a new
candidate name. Existing immutable sources are checked before reuse. Reports
are saved inside the chosen arena, alongside its database and full replays.
An optional final candidate ID selects a historical candidate for the report;
each report is also retained as `evaluation_report_ID.json`.
