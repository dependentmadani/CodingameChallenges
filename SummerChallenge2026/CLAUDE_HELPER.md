# CLAUDE.md

Working notes for the CodinGame **Summer Challenge 2026** railway agent. Kept short on purpose.
Claims marked *(notes)* come from earlier sessions and were not re-checked on 2026-09-13 — verify
before relying on them. Everything else was measured or checked in the repo.

## Ground rules

- **Carlo launches real training runs.** Smoke-test a config for 2–3 iterations against a scratch
  `checkpoint_dir`/`log_dir`, stop it, and hand back the command. Baking, evaluating and
  `test_submission.py` on existing artifacts are fine; stop them when done.
- **Pick checkpoints from the eval table, never the newest.** Runs here peak early and decay.
- **Ask before deleting** checkpoints or anything in `baselines/`.
- Repo: private, `github.com/CarloCDT/summer-challenge-2026`, branch `main`. `checkpoints/` is
  untracked, so the checkpoints listed under Housekeeping exist only on this machine.

## Rules that are easy to get wrong

- 100 turns. Each turn: 3 paint points (plains 1 / river 2 / mountain 3) and 1 disruption point.
  Neither carries over.
- A connection pays **1 point per turn per own track on its path** while active. Neutral tracks
  and towns pay nobody.
- The scoring path is the one with the **fewest cells** (BFS, N→E→S→W tie-break). Terrain cost only
  affects what you pay. `AUTOPLACE` uses a cost-weighted search instead.
- Instability 4 inks a region and destroys **both** players' track. Town regions can't be inked.
- Both players placing on the same cell makes it neutral, and both pay.
- The game ends early once no desired connection is reachable.
- Source of truth: the Java referee in `SummerChallenge2026/`. `verify_rules.py` (62 checks) must
  stay green.

## Live pipeline

| step | file |
|---|---|
| environment (faithful port) | `railroad_env/` |
| board → 20×30×38 canvas + action masks | `training/encoding.py`, `railroad_env/features.py` |
| bake runtime vs `encode_state`, every channel, both seats | `verify_bake_parity.py` |
| model | `training/agent_model.py` (`RailroadUNet`) |
| PPO | `training/train_ppo.py`, `training/ppo.py` |
| imitate `level2MC` (DAgger, shadow expert labels, STOP_PLACING) | `training/imitate_mc.py` |
| teacher → student, int4 QAT | `training/distill.py`, `training/quantization.py` |
| checkpoint → submission (≤100,000 chars) | `bake_agent.py` → `submission.py` |
| debug bake that prints P(win) — never submit | `bake_debug_agent.py` |
| play a bake over the wire protocol | `test_submission.py` |
| greedy boss table | `evaluate_checkpoints.py` |
| head-to-head, both seats | `compare_agents.py` |
| behaviour read-out | `diagnose_agent.py` |

```
python3 -u -m training.train_ppo --config training/configs/<config>.yaml
python3 -u -m training.distill --config training/configs/distill.yaml
python3 bake_agent.py checkpoints/<student>.pt -o submission.py --quant int4
python3 test_submission.py submission.py --opponent level2Silver4 --episodes 50
python3 compare_agents.py a=<file.pt|file.py> b=<file.pt|file.py> --episodes 25 --workers 8
```

Legacy, still importable, not used: the AlphaZero/MCTS stack (`training/train.py`, `mcts.py`, …)
and the lite game (`railroad_lite_env.py`, `training/lite*.py`). Don't extend them without asking.

## v2 — the from-scratch rebuild (2026-09-15)

A second, self-contained agent in `v2/`: its own C++ engine, features, network, training and C++
submission, sharing no code with the above. See `v2/README.md`. Built because the pipeline above is
env-bound and spends four forward passes per turn; v2 steps 512 games in ~5 ms and scores a whole
turn (up to three placements plus a disruption) as one action from one forward pass.

- `python3 -u -m v2.imitate --config v2/configs/imitate.yaml` — clone `level2MC` in-process.
- `python3 -u -m v2.train --config v2/configs/overnight.yaml --set init=<clone ckpt> name=<run>`
- `python3 -m v2.bake <ckpt> -o v2/build/submission.cpp` — 97k chars at int4, ~4 ms per turn.

Measured on 2026-09-15: cloning MC for 9 minutes gives 0.31 against `mc_i24` and 0.81 against the
scripted builder; 60 iterations of PPO from scratch (no clone) reached only 0.02 against that
builder. int8 would be 163k chars, over the limit, so int4 it is; paired on 160 maps it cost
0.056 ± 0.037 win rate against `mc_i24` (suggestive, not proven). Rules parity against
`railroad_env` is checked on 24k turns per run of `v2/tests/test_rules_parity.py`.

**A shared value head is fatal here.** Fitting a fresh critic through the same trunk unlearned the
clone's whole policy in ~5 iterations (entropy 8.1 → 3.6, placements 2.0 → 0.56, self-play score
1800 → 112), reproducing arm D's old finding. `critic: separate` fixes it; `train.py` warm-starts the
critic from the checkpoint so later legs keep their value function.

**The 100k limit counts UTF-16 code units, verified by a real paste on 2026-09-16.** So weights pack
into the CJK block (U+4E00+, 14 bits per character) instead of base85's 6.4: `bake.py --encoding cjk`.
The same net is 97,462 chars at int4/base85, 59,003 at int4/CJK and 89,325 at int8/CJK (214,741 bytes
on disk - the byte count is irrelevant). **int8 is therefore shippable, and it beat the int4 QAT
artifact 0.564 ± 0.028 over 320 paired games**, differing from the float checkpoint on 14.4% of
decisions against int4's ~86%. Two consequences: QAT is bin-width-specific and now pointless (the
int4-QAT checkpoint re-baked at int8 scored 0.411, worse than either endpoint - bake int8 from the
pre-QAT float master), and the budget would allow ~2.2x the parameters at int4/CJK if capacity ever
becomes the limit.

**Quantization-aware fine-tuning is worth an hour** (2026-09-16, `v2/qat.py`, `configs/qat.yaml`).
200 iterations at lr 2e-5 training through the bake's own int4 rounding (straight-through, quantizer
verified bit-identical to `bake.py`) beat the previous artifact **0.588 ± 0.028 head to head over 320
paired games (+3.2 sigma)**. Pick by head-to-head, not by eval: at 96 games iter00175 looked ahead of
iter00200, and at 320 games the order reversed. On a fixed 128-map set against full-budget
`level2MC`, int4 iter01200 scored 0.836 ± 0.033 and QAT iter00200 0.852 ± 0.032 - note the same
artifact scored 0.781 on a different 64-map set, so map-set noise swamps differences below ~0.05.

**Two overnight legs, 2026-09-15/16.** Leg 1 (`overnight.yaml`, pool rung `mc_i12`) solved its rung
within ~300 iterations - every pool opponent saturated (snapshots 0.93-0.95, greedy 1.00). Leg 2
(`leg2.yaml`, rung `mc_i96` at 20% of games, snapshot pool cut 40 → 8, 1400 iterations in ~10 h)
improved to the end with no decay: `eval_mc_i96` 0.72 → 0.91. On an idle machine,
`20260915-220926/iter01200` beats **full-budget `level2MC` 0.875 ± 0.042** and the old ranked
`submission.py` 48/48.

**Leg 3 shipped the current submission** (`leg3_hard.yaml`, run `20260916-112610`, init
`20260915-220926/iter01400`). A VS Code update SIGHUPed it at iteration 400 of 410 - no error in the
log, nothing lost. Its `eval_mc_i384` column never trended: 0.71-0.84 all run, which at 128 games
(±0.033) is noise end to end. Picking by that column alone would have been guesswork, so iter00400
was confirmed by match play, and **`v2/build/submission.cpp` now holds `20260916-112610/iter00400`,
baked int8/CJK at 89,325 chars.** On 500 fresh maps (seeds 7,000,000+), both artifacts at int8/CJK:

| | vs full `level2MC` | head to head |
|---|---|---|
| `20260916-112610/iter00400` | **0.883 ± 0.014**, margin +547 | 0.514 ± 0.022 |
| `20260915-220926/iter01300` (previous) | 0.830 ± 0.017, margin +443 | - |

Paired on the same maps the gap against MC is **+0.053 ± 0.021** (McNemar p = 0.016 on 108 discordant
maps). Note the two are a coin flip *against each other* while one is clearly better against MC:
**head-to-head between near-mirror policies does not measure strength** - the only hint was margin
(+42.7 ± 18.4) leading the win rate. Rank candidates against a third party, not each other.

Two measurement traps found doing this, both in `v2/vs.py`:

- **Swapping seats on the same map seed reproduces the game exactly.** Turns are simultaneous, so
  there is no seat advantage, and the bakes play greedy and deterministic. A "paired" seat-swap
  design therefore duplicates games instead of controlling anything, halving the real sample and
  understating the SE by √2. Give every game its own seed; pair *across artifacts* by reusing the
  seed set. Two identical bakes still score 0:0 on every game, which is the harness self-test.
- **`level2MC`'s only variability is its 46 ms deadline** - its RNG seed is fixed
  (`make_random(0x12345678u, 2*turn+player)`) - so CPU contention silently weakens it. On this
  32-core machine it holds ~1400 simulations/turn at ≤8 workers and loses 15-25% at 16-24. Always
  report its simulations/turn alongside any full-budget MC number.

## Observation — 38 channels

| ch | contents | ch | contents |
|---|---|---|---|
| 0–2 | terrain one-hot | 22 | instability / 4 |
| 3, 4 | enemy / own track | 23 | region inked (padding counts as inked) |
| 5, 6 | enemy / own tracks in region, count / 10 | 24 | cell on an active connection |
| 7 | region size / board cells | 25 | town here |
| 8, 9 | enemy / own tracks in region on an active connection, count / 10 | 26 | (own − enemy score) / 10000, flat |
| 10–21 | route guides: −1 this town, −0.5 wanted town, +1 between | 27 | turn / 100, flat |

Channels 28–37 are derived features from `railroad_env/features.py` (added 2026-09-14):

| ch | contents | ch | contents |
|---|---|---|---|
| 28 | active connections through the cell / 10 | 33 | region contains a town (can't be inked) |
| 29, 30 | own / enemy income at stake in the region / 20 | 34 | not-yet-active connections whose cheapest completion crosses the cell / 5 |
| 31 | tanh(score diff / 300), flat | 35 | 1 / cheapest remaining completion cost there |
| 32 | tanh(income-rate diff / 30), flat | 36 | share of desired connections still reachable, flat |
| | | 37 | instability the opponent added to the region / 4 |

Channels are only ever **appended**. `RailroadUNet` slices its input to its own `in_channels`, so
28-channel checkpoints still load and play, and the frozen `level2Silver*` bakes carry their own
28-channel runtime. `train_ppo.py` warm-starts a wider net by giving the new stem inputs zero
weights, which leaves its function unchanged.

**Changing the layout means edits in lockstep:** `railroad_env/features.py` (or
`game_state.py::get_observation`), its copy in `bake_agent.py`'s runtime, `CHANNEL_NAMES` in
`debug_full_agent.ipynb`, and `verify_rules.py::test_derived_features` if a meaning changes. Then run
`python3 verify_bake_parity.py`, which compares the bake's `observe()` with `encode_state` from both
seats.

## Opponents

- `level1` / `passive` waits. `level2` / `boss` autoplaces between two random towns. These are the
  shipped bosses.
- `level1Pro` skips placing half the time and disrupts half the time. `level2Pro` builds the
  cheapest unfinished connection and never disrupts. `level2ProMax` is `level2Pro` plus inking where
  enemy track leads by more than one.
- `level2Silver` … `level2Silver4` are frozen bakes in `baselines/`, run as subprocesses, ~1 s per
  game. Rungs are added, never repointed, so old numbers keep their meaning. They're stateful per
  game and can't be deep-copied mid-game. See `baselines/README.md`.
- `level2MC` is a C++ MCTS search bot from the arena (`baselines/search_mcts.cpp`, not our lineage),
  compiled on first use into `baselines/bin/`. It looks 24 turns ahead within 46 ms per turn (900 ms
  on turn one), so its strength depends on free CPU and each game costs ~5.5 s of its search. On
  2026-09-14, with a training run loading the machine, it beat the D-student `submission.py`
  0.73 ± 0.07 over 48 games (seats alternated, 0 illegal actions). `compare_agents.py` takes
  registered names, e.g. `mc=level2MC`.
- The MC ladder (`level2MC_i1`, `_i12`, `_i24`, `_i96`) caps its search iterations, so strength is
  independent of machine load. D175 wins 0.67 / 0.52 / 0.31 / 0.22 against them and 0.30 against
  full `level2MC`. Below 12 iterations MC doesn't search at all. Train against the rung near 50%
  (currently `_i12`); see `baselines/README.md`.

## What we've learned

Measured in this project, most recent first:

- **Stop-only imitation fixed track waste but not income** (`20260914-200335-imitate`, stage 1).
  The greedy stop rate matched MC's (~20%) by iteration ~8; later iterations only eroded the Silver
  evals (Silver3 0.73 → 0.69 → 0.63, Silver4 0.64 → 0.69 → 0.55 at iterations 5/10/15, 100 games).
  On 16 paired maps against `level2MC_i24`, iter10 against D175:
  - It placed 91 tracks instead of 159.
  - 76% of them ever carried a paying connection (D175 45%, MC 68–77%), earning 6.8 per placed
    track over turns 1–30 (D175 4.0, MC 6.8–7.6).
  - Its total income over those turns was unchanged, 572 vs 589, still below MC's ~650.
  - MC's own paying track-turns rose from 691 to 894 against it, so D175's extra track had been
    getting in MC's way.

  The remaining gap is route choice and contesting, which is stage 2's job. iter10 is the stage-2
  start.

- **STOP_PLACING (4th policy channel) warm-starts exactly only in one construction.** The mask must
  be exactly the legal PLACE cells, and channel 3 must start as a copy of PLACE plus an offset b.
  Then P(stop) = sigmoid(b) in every state (0.0180 at b = −4.0, checked over 364 states), and greedy
  play (total-mass rule, below) matched D175 on 529/529 decisions. Two alternatives fail on D175:
  - A constant logit fails because absolute PLACE logits drift −10…+24 nats between states, so
    greedy play would stop on ~30% of decisions.
  - A whole-board stop plane fails because masked cells (track, towns) never train and sit ~33 nats
    above the legal ones, so greedy would stop ~97% of the time.
- **Imitating MC at lr 1e-4 wrecks the policy within 2 iterations.** On 48 fresh maps the smoke
  checkpoint fell from D175's 0.708 to 0.188 against `level2Silver4`, and from 0.500 to 0.125
  against `level2MC_i12`. Placement entropy went 1.11 → 4.60: D175's confident choices disagree
  with MC's labels, and big steps flatten the policy before it learns anything. At lr 2e-5, six
  iterations (32 games each) still hurt: 0.354 / 0.125 without an anchor, 0.542 / 0.292 with
  `anchor_kl_weight` 0.2, and placement entropy ~3.2 either way. Matching MC's exact cells only
  rose from 11% to 15%, while its stop decisions were learned fast (stop accuracy ~0.8). So
  imitating WHERE MC builds is what hurts; `components` limits imitation to stop and/or disrupt.
- **Greedy STOP must compare total mass, not single cells.** That same checkpoint sampled STOP 11%
  of the time, yet per-cell argmax chose it 0%, because the stop probability is spread over every
  legal cell. `simulator.greedy_index` (used by eval, self-play, distill, imitation and the bake
  runtime) compares the stop logsumexp with the best placement. Under that rule D175's
  logsumexp-minus-max over legal placements never exceeds 3.59, so b = −4.0 keeps the warm start
  exact while sampling stops 1.8% of the time.
- **D175 and MC barely agree at the start of imitation:** D175 matches MC's placement choice 12%
  of the time and its disrupt region 33%. MC stops placing on 21% of PLACE decisions.

- **MC beats us on track efficiency, and our whole lineage wastes track** (2026-09-14, D175, 16 games
  each). Against `level2MC`, 44% of our placed tracks ever carry a paying connection, against 77% of
  MC's. We place 162 tracks, MC places 101, and MC earns 8.1 per placed track over turns 1–30 against
  our 4.9. Against `level2Silver4` both sides are ~36–41%, so the self-referential eval never showed
  it. MC builds only on its chosen routes, never in a region at instability ≥ 3 without a town, and
  may leave paint unspent. `GameSimulator` forces us to spend all 3 paint every turn.
- **Training mostly against full-strength MC decays** (`20260914-175902`, pool MC 0.7): ~27% win
  rate, entropy 2.1 → 2.5, and `level2Silver3`/`level2Silver4` fell to 0.65 by iteration 50. It
  gives too little positive signal. Use a handicapped MC near 50% instead.

- **38-channel arms A (control, `20260914-125003`) and D (separate critic + shaping,
  `20260914-103649`) both improved a lot.** Both started at 0.64 against `level2Silver3` and 0.46
  against `level2Silver4` and ended around 0.72–0.79 and 0.62–0.68, so most of the gain comes from
  the new channels and the shared opponent pool. D's extra edge is modest. On 400 fresh maps
  (seeds 20000+), D175 − A175 was +0.087 ± 0.030 against Silver3, +0.025 ± 0.034 against Silver4,
  and +0.056 ± 0.023 across both. Head-to-head, D175 beat A175 0.54 ± 0.035 (margin +132). D also
  got there ~75 iterations sooner. D175 and D200 are indistinguishable. Arms B and C were skipped.
  `submission.py` is now the 38-channel bake of D175's student (`20260914-140729-distill_iter50`,
  92,437 chars).

- **A pool worker can die natively, and `multiprocessing.Pool` then hangs forever.** Run
  `20260914-093944` (arm D) trained 25 iterations, then one worker segfaulted inside libpython as
  the first eval began (`dmesg`), and the trainer sat at 0% CPU with no error. The live pools
  (`train_ppo.py`, `distill.py`, `evaluate_checkpoints.py`, `compare_agents.py`) now use
  `training/resilient_pool.py::ResilientPool`, which re-runs the dead worker's tasks on fresh
  workers. Workers print a faulthandler traceback when they crash, so check the log for one.
- **Training runs in the `rl` virtualenv** (`~/.virtualenvs/rl`, Homebrew Python 3.11.12, torch
  2.12.1, numpy 2.4.6). A bare `python3` outside it is the system 3.10 stack with a different torch
  and numpy, so smoke-test with the virtualenv's interpreter.

- **2026-09-14 diagnosis of the plateau**, all on `065817_iter200` (the scripts weren't kept):
  - The value loss steered the shared encoder. Against `level2Silver4` its gradient norm was ~32
    against ~7 for the policy loss, near-orthogonal, and `max_grad_norm` 0.5 clipped every
    minibatch. The entropy term's gradient was ~0.01, which is why changing `entropy_coef` did
    nothing. Arms B–D target this (`value_scale`, a separate critic).
  - The critic can't call a contested game early: corr(V(turn 0), outcome) +0.06, mid-game +0.66.
    The per-game sum of margin rewards has sd 0.62, against the ±5 win bonus. Arm D's shaping
    targets this.
  - Against a near-mirror (`level2Silver4`), 24% of our placements collide into neutral track (3%
    against `level2Pro`), 42% of our own track gets inked, and games end at turn ~63 with ~480
    points each.
  - Channel 26 reads ~0.005 in contested games (margins ±100 over a 10,000 divisor), hence channel 31.
  - A disrupt is spread over its region's cells, which adds ~0.64 nats of meaningless entropy to
    disrupt decisions. Read `train/entropy` with that in mind.
  - CodinGame's 100k limit counts UTF-16 code units, so a CJK-range payload carries 15 bits per
    character against base85's 6.4. The current student would be ~44k chars at int4 or ~82k at
    int8. Not built.

- **Long PPO runs from the current lineage decay.** `065817` (200 iterations, 2 epochs, fast lr
  anneal) is the only recent run that improved. The 500-iteration, 3-epoch attempts (`130019`,
  `164104`) pushed entropy past 2.0 and lost ground on every eval column. Halving the entropy bonus
  did not slow the climb.
- **Healthy optimiser metrics don't mean learning.** `074038`, `222648` and `130019` all kept
  clip fraction, KL and explained variance in range while specialising, stalling and decaying.
  Only eval tables show progress, so keep a held-out rung in `eval_bosses`.
- **Against contested opponents the reward is roughly one bit per game.** The per-turn margin is
  about 1 point (0.001 after `score_norm` 1000), against ~160 per turn versus `level2Pro`. The ±5.0
  win bonus dominates the return. This is the likeliest cause of slow, noisy learning. Candidate
  fix, not built: potential-based shaping on projected remaining income.
- **γ 0.998 / λ 0.99 hurt** (`222648`): advantage std up 46%, explained variance 0.87 → 0.77. Keep
  0.997 / 0.98.
- **Don't anneal lr to zero.** Use `lr_end` ≈ 20% of peak. 2.5e-5 peak was too cold.
- **Self-play is worth its compute under `win_bonus`.** Mean |advantage| per decision was 1.196 for
  `self`, second only to `level2Pro`. `self` mirrors the live weights, so it never ratchets.
- **`distill/kl` and `distill/argmax_agreement` get worse at the end of every run, and that's an
  artifact.** They're training losses on each iteration's fresh games. Late in the lr anneal
  (~1e-5) the student stops fitting a batch within its 4 epochs, so the number turns into a
  held-out score. Scored on the same fixed states, `20260914-140729-distill` iter50 beat iter25
  everywhere (teacher-driven states: KL 0.99 vs 1.35, agreement 0.45 vs 0.39, int4). Honest
  held-out KL is ~1.0, about double the logged value: the student sees only 800 games and has no
  replay.
- **Distillation did not lose the contested edge** (2026-09-14, 128 paired seeds against
  `level2Silver3`): teacher fp32 0.62, its int4 bake 0.63, an int8 re-bake of the same student 0.66,
  all within one SE (±0.043). The earlier 0.73 vs 0.62 was noise, and so is the old "int4 costs
  ~0.07" note below as far as this matchup shows. `distill.yaml` still runs only 50 iterations.
- **Value distillation works.** `value_loss_weight` 0.25 with `value_huber` 2.0 gives the student a
  critic correlating ~0.975 with the teacher, at no policy cost and no submission size.
- **Win-probability fits need losses.** Against an opponent won ≥0.9, the fit collapses to the base
  rate. Calibrate against a near-even rung.
- **`action_selection: onpolicy` rollouts aren't reproducible from a seed** (global torch RNG). Use
  `epsilon_greedy` with `epsilon: 0` for A/B tests.
- **Two identical agents score 0:0** (every contested cell goes neutral). Use it to check a rung
  points at the right file.

*(notes)*, not re-checked:

- Our eval table doesn't predict arena rank. Only the `114307_iter200` lineage (rank ~288) has
  arena evidence.
- BatchNorm must stay in eval mode during the PPO update. The pin is in `ppo.py::ppo_update`
  (checked); the original finding, that train-mode BatchNorm broke the PPO ratio, is from notes.
- Masked-off actions get zero gradient. `force_disrupt` is recorded in checkpoints and honoured by
  eval and bake.
- int4 costs ~0.07 win rate against fp32 head-to-head; `quant_aware: int4` targets that. Judge
  quantization by match results, not move agreement.
- The bake must answer in 50 ms per turn and runs ~3 ms median. Keep its stderr small, since the
  referee may not drain the pipe.
- Rollout is ~93% of iteration time, mostly `GreedyAutoplaceOpponent` re-pricing (no cache yet).

## Current state — 2026-09-14 (evening)

- **Best teacher:** D175, `checkpoints/20260914-103649_iter175.pt` (arm D). `submission.py` is the
  38-channel bake of its student `20260914-140729-distill_iter50` (92,437 chars). Two D2 runs from
  D175 were stopped without beating it: `20260914-155802` (plain pool) and `20260914-175902` (70% MC).
- **The target is now `level2MC`,** which beats D175 ~0.70. Built for it, and smoke-tested end to end
  (imitate → PPO → distil → bake → parity → `test_submission.py`):
  - STOP_PLACING, a 4th policy channel. It has an exact warm start and a total-mass greedy rule; see
    "What we've learned".
  - The iteration-capped MC ladder: `level2MC_i1`, `_i12`, `_i24`, `_i96`.
  - `training/imitate_mc.py`. Imitating only MC's stop decisions (`components: stop`, lr 2e-5,
    `anchor_kl_weight` 0.2) kept D175's placement entropy (1.10) and matched MC's greedy stop rate
    (~18%) in 6 short iterations. It scored 0.625 / 0.458 against `level2Silver4` / `level2MC_i12`,
    against D175's 0.708 / 0.500 (48 games, within noise). Adding disrupt or placement labels did
    worse.
- The contest ends **2026-09-21**. Arena results aren't recorded in the repo; add them here when known.

## Next steps

1. **Stage 1:** `training/configs/imitate_mc.yaml` (stop-only, 30 iterations, evals every 5). Pick a
   checkpoint that keeps `level2Silver4` near D175 and has the best MC-rung numbers.
2. **Stage 2:** `ppo_38ch_e_vs_mc.yaml --init-checkpoint <stage-1 pick>`. Once `level2MC_i12` wins
   exceed ~0.65, restart from the best checkpoint with more weight on `_i24` / `_i96`. Arm E0 is the
   same config without `--init-checkpoint` (from D175), the control if time allows.
3. **Ship:** distil with `distill_38ch_e.yaml --teacher <pick>`, bake, `verify_bake_parity.py`,
   `test_submission.py`, then `compare_agents.py` against `submission.py` and `mc=level2MC`, submit.

## Housekeeping

- **Keep:** `checkpoints/20260910-224257_iter200.pt`, `20260911-114307_iter200.pt`,
  `20260912-155239_iter500.pt`, `20260913-065817_iter200.pt`, and everything in `baselines/`.
- `checkpoints/` holds 184 files, 3.4 GB. 88 are 25-channel and won't load; check
  `model_kwargs["in_channels"]` first.
- `ppo_finetune_student.yaml` has a `REPLACE_ME` path and targets a 25-channel student. Unusable.
- `/home/carlo/claude_env` is a standalone copy of the environment for others to train against.