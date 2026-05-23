# SC2026 auto-tuner

Random-searches over MCTS heuristic constants in `main.cpp`. For each
trial: regex-patches the source with sampled values, compiles, runs N
arena games vs `other_code.cpp`, records mean/wins/full deltas.

## Files

- `tuner.py`          — Python orchestrator (resumable, single-process)
- `search_space.json` — parameter ranges + trial budget
- `Dockerfile`        — container with g++ + Python
- `results/`          — output (mounted volume in Docker)
  - `runs.jsonl`      — one row per completed trial
  - `best.json`       — current best config
  - `progress.log`    — human-readable log

## Run locally (no Docker)

```bash
# Quick smoke (2 trials × 2 games — ~1 min)
TUNER_RESULTS=/tmp/tuner_smoke TUNER_TRIALS=2 TUNER_GAMES=2 \
  python3 tuner/tuner.py

# Full run (60 trials × 8 games — ~40-60 min)
python3 tuner/tuner.py
# results in ./tuner/results/
```

## Run in Docker

```bash
# From the SpringChallenge2026 directory:
docker build -t sc2026-tuner -f tuner/Dockerfile .

# Mount results so they persist on the host:
docker run --rm -v "$PWD/tuner/results:/results" sc2026-tuner

# Override trial count / games for a smaller run:
docker run --rm \
  -e TUNER_TRIALS=20 -e TUNER_GAMES=4 \
  -v "$PWD/tuner/results:/results" \
  sc2026-tuner

# Background + tail logs:
docker run -d --name sc2026-tuner \
  -v "$PWD/tuner/results:/results" \
  sc2026-tuner
docker logs -f sc2026-tuner
```

## Resumability

If the container is interrupted and re-started, the tuner reads
`runs.jsonl` and skips configs it already evaluated. Delete the
results directory to start fresh.

## Scoring

Each trial's `score = mean_delta + 20 * wins`. The +20/win bonus is
lexicographic-ish: a config that wins 4/8 with mean -10 (score 70)
beats one that wins 0/8 with mean +5 (score 5). This biases toward
configurations that produce occasional wins rather than consistently
close losses.

## After a run completes

```bash
# Top 10 configs by score:
jq -s 'sort_by(-.score) | .[:10]' results/runs.jsonl

# Best config to copy into main.cpp:
cat results/best.json

# How often each parameter value won:
jq -s 'group_by(.config.per_troll_bonus) | .[] | {value: .[0].config.per_troll_bonus, avg_score: ([.[].score]|add/length), n: length}' results/runs.jsonl
```

## Search space

See `search_space.json`. Each parameter has:
- `anchor` — human-readable form of the patched line
- `old_value_regex` — regex with capture group 1 = the value to replace
- `values` — list of candidates to sample from

Add/remove parameters by editing the JSON — no code changes needed.

## Tuning the tuner

- `random_seed`: deterministic sampling. Re-running with the same seed
  picks up where we left off (resumable).
- `trials`: how many configurations to try. Random search converges
  quickly with ~30-60 trials for ~8 parameters.
- `games_per_trial`: lower variance with more games. 8 is a reasonable
  bias-variance tradeoff; bump to 16 for high-confidence final ranking.
