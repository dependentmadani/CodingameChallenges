# Weight tuning bundle

Tunes a coherent subset of `../main.cpp`'s continuous weights against a fixed
reference build. Integer/structural settings remain fixed for separate sweeps.
Designed to be copied onto a cloud box and left running.

## Read this first: production-equivalent timing

Every budget gate in `main.cpp` is wall-clock based (`elapsedMs() > 29.0` and
friends), so the bot's decisions depend on how busy the machine is. Measured on
the real referee:

* identical binaries playing each other scored **55.9%**, not 50%
* single-parameter probes swung **−206 to +203** score margin on pure noise
* the noise floor was about **±200 margin per 80 matches**

At that noise level, detecting a genuine +50 margin effect needs roughly **1,300
matches per trial** — thousands of CPU-hours across 53 parameters. Not viable.

The tuner now keeps the exact production wall-clock gates. Disabling them made
the tuning build enumerate candidates that the submitted bot never sees and
caused timeouts even at a reduced tree iteration count. `selfcheck.py` runs the
same production-equivalent binary under the requested concurrency and compares
complete action streams, not only final scores. SPSA refuses to start unless it
passes. If the check fails, lower both the tuning and intended production
`P_TREE_ITERS`, or use a quieter host; do not tune one work budget and ship another.

## The search cap and why it is 24

`P_TREE_ITERS` is 24, with separate 20 ms admission and 30 ms emergency-stop
guards on the reserved clock. At the previous 250 cap, the old 18 ms cutoff
truncated about 48.7% of searches and **0 of 8 seeds reproduced**, which made
tuning impossible: a parameter change could not be told apart from run-to-run
noise.

Root portfolios are ranked and capped at eight actions, repeated first-ply
transitions are cached, and opponent-route preprocessing has a work cap. A late
search is discarded in favor of the already-computed legal heuristic action.
The final disruption recomputation is skipped when the turn is late as well.

A 48-iteration experiment scored 53-43 against 24 iterations, but later referee
runs still produced timing forfeits. It is therefore not shipped. The guarded
24-iteration build completed an 80-game, two-worker contention test with zero
forfeits or rejected actions, scoring 43-37 against `main_bot.cpp`.

`--allow-noisy` exists for running against a build that cannot pass the gate
(such as a 250 build). It is not needed at 24 on a quiet host, and using it with
small `--seeds` mostly moves the vector on noise.

## Never use seed 0

The referee treats seed 0 as a sentinel and **generates a different map on every
run**. Verified by capturing the bot's stdin: seed 0's initial map hash differs
between runs, seed 1's is identical. Any seed range including 0 mixes an
unreproducible game into the average. Every tool here now defaults to starting
at seed 1; keep it that way in any range you pass by hand.

## Quick start (Docker — recommended for cloud)

```sh
docker build -t sc2026-tune .                       # from the repo root
docker run --rm --cpuset-cpus=0-3 sc2026-tune       # runs selfcheck, must PASS
docker run --rm --cpuset-cpus=0-7 sc2026-tune \
  python3 tuning/spsa.py --group search --iters 80 --seeds 24 --workers 8
```

Pin cores with **`--cpuset-cpus`**, never limit with `--cpus`. A `--cpus` quota
throttles the container at every cgroup period boundary, and those stalls are
exactly what makes the referee drop a late action — it manufactures the
nondeterminism this setup exists to avoid.

Do not use Docker Desktop on macOS/Windows for a real run: it adds a VM
scheduling layer on top of an already-busy desktop. Build and test the image
there if you like, then run it on a Linux host.

## Quick start (no Docker)

```sh
export JAVA_BIN=$(which java)                       # Linux default is fine
export REFEREE_JAR=/path/to/referee.jar             # ~13 MB, from arena_tools/

python3 tuning/selfcheck.py --workers 8 --tree-iters 24   # gate: must PASS
python3 tuning/spsa.py --group search --iters 80 --seeds 24 --workers 8
```

Needs only `java` (JDK 17+; the jar's highest class-file version is 61) and
`python3`, plus `g++` or `clang++`. No third-party packages.

## Sizing

About 3s per match, and SPSA plays 2 x seeds x 2 seats per iteration — so 24
seeds is ~96 matches, ~290 CPU-seconds, per iteration:

| cores | 80 iterations |
|---|---|
| 8 | ~1 hour |
| 4 | ~2 hours |
| 2 | ~4 hours |

If `selfcheck` fails, lower `--workers`, move to a quieter machine, or deliberately
lower `--tree-iters` in both tuning and the final build.

`selfcheck.py` is not optional. It replays the same seeds twice at the requested
concurrency and checks their full action streams agree. If they do not, the referee is dropping late actions because
turns are running near the limit, and **tuning will optimise noise instead of
play**. A developer laptop with a browser open typically fails this; a quiet
cloud instance passes. If it fails: close CPU users, lower `--workers`, or lower
`--tree-iters`.

## Files

| file | role |
|---|---|
| `params.json` | all 53 exposed settings: default, low, high, int/float |
| `spsa.py` | the optimiser |
| `arena.py` | paired match runner (also usable standalone) |
| `selfcheck.py` | determinism gate — run before any long job |
| `referee_adapter.py` | referee wrapper; JVM and jar paths from env |

## Why SPSA

The earlier coordinate hill-climb moved one parameter per evaluation. SPSA
perturbs every selected continuous parameter at once and needs two evaluations
per iteration regardless of dimension. Use `--group search` (default), `attack`,
or `planner`; `all` is available for experiments but is much harder to identify.
Rounded integers are deliberately excluded because, for example, depth 3 rounded
to 3 on both sides of an SPSA probe while still receiving an unrelated update.
Evaluate integer settings with explicit discrete comparisons instead.

Scores are paired per seed and both seats. The objective is 80% match outcome
and 20% `tanh(score_margin / 500)`: it follows the actual win condition while
retaining a bounded, smoother signal. A blowout cannot outweigh many losses.
Any bot forfeit or incomplete pair invalidates the entire evaluation.

## Interpreting output

`f+` / `f-` are the two probe objectives; individual values mean little. The
periodic `select@N` line measures the current vector on a model-selection range
(`--validate-start`, default 100000). A candidate is saved only when its lower
confidence bound clears the default. After optimization, the selected vector is
evaluated once on an untouched final range (`--test-start`, default 200000).

Trust the untouched final test, not individual probes or repeated selection scores.

## Shipping a result

```sh
cat tuning/best.json # parameters plus source hash, settings and test metrics
```

Before believing any of it:

1. copy only the `parameters` values from `best.json`
2. build with the same `P_TREE_ITERS` used during tuning
3. `python3 test_bot.py ./bot` — all tests must pass
4. validate on a new seed range never used by training, selection, or final test
5. check turn time still clears 50 ms with margin

## Known limits

* The runner uses the author's league-3 referee and map generator, not
  `test_bot.generated_board()`. It still tunes against one fixed bot lineage,
  so validate against multiple frozen opponents before promotion.
* `P_*` weights were already tuned once (54 trials, 0 accepts) and are likely
  near a local optimum. The untouched value is in the `S_*` (tree search) and
  `A_*` (greedy disruption) groups.
* The selected weights are conditional on the chosen tree iteration budget.
  Do not infer that weights tuned at one cap transfer to another.
* The shipped `P_TREE_ITERS` is 24 and tuning must use the same value; see
  "The search cap and why it is 24". Weights found at one cap are conditional
  on it and should not be carried to another.
