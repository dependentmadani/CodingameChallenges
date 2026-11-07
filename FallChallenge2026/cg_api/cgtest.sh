#!/bin/bash
# Run CodinGame test games of a bot file against agents, one at a time.
#   cgtest.sh BOT.cpp TAG AGENT_ID[:SEED] [AGENT_ID[:SEED]...]   (SEED fixes the map, for paired comparisons)
#   cgtest.sh --summary LOG...        (summarize logs already saved)
# Logs go to cg_api/logs/TAG_<n>_<agent>.log. One summary line per game: result, scores, our seat,
# turn count, our average search iterations and slowest turn (from our stderr "turn N iters A/B t MS").
set -u
API=https://www.codingame.com/api/v1
DIR=$(cd "$(dirname "$0")" && pwd)
cg() { curl -s -H "Authorization: Bearer $(cat ~/.config/codingame/token)" "$@"; }

summary() {  # $1 = text log
  local L=$1
  local ME NAME SC S0 S1 MY OP RES STATS TO
  ME=$(grep -E '^  [01] .*\(me' "$L" | head -1 | awk '{print $1}')
  NAME=$(grep -E '^  [01] ' "$L" | grep -v '(me' | head -1 | awk '{print $2}')
  SC=$(grep 'scores:' "$L" | sed -E 's/.*0=(-?[0-9]+) 1=(-?[0-9]+).*/\1 \2/')
  S0=${SC% *}; S1=${SC#* }
  if [ "$ME" = "0" ]; then MY=$S0; OP=$S1; else MY=$S1; OP=$S0; fi
  RES=DRAW; [ "$MY" -gt "$OP" ] 2>/dev/null && RES=WIN; [ "$MY" -lt "$OP" ] 2>/dev/null && RES=LOSS
  STATS=$(grep -E '^ +turn [0-9]+ iters [0-9]+/[0-9]+ t ' "$L" | awk '$2>1 {split($4,a,"/"); s+=a[1]; c++; if ($6>m) m=$6} END {if (c) printf "iters %5.0f  max_t %5.1f ms  turns %3d", s/c, m, c+1}')
  TO=$(grep -cE 'has not provided|timeout' "$L")
  printf "%-4s %4s-%-4s vs %-16s seat %s  %s  timeouts %s  %s\n" "$RES" "$MY" "$OP" "$NAME" "$ME" "$STATS" "$TO" "$(grep -oE 'seed=-?[0-9]+' "$L" | head -1)"
}

if [ "${1:-}" = "--summary" ]; then shift; for L in "$@"; do summary "$L"; done; exit 0; fi

BOT=$1; TAG=$2; shift 2
mkdir -p "$DIR/logs"
n=0
for ARG in "$@"; do
  OPP=${ARG%%:*}; SEED=""; [ "$ARG" != "$OPP" ] && SEED=${ARG#*:}
  case "$OPP" in ''|*[!0-9]*) echo "not an agent id: $OPP"; continue ;; esac
  n=$((n + 1))
  OUT="$DIR/logs/${TAG}_${n}_${OPP}"
  while true; do
    jq -n --rawfile code "$BOT" --argjson o "$OPP" --arg s "$SEED" '{language: "C++", code: $code, opponents: [$o]} + (if $s == "" then {} else {game_options: ("seed=" + $s)} end)' \
      | cg -X POST "$API/challenges/fall-challenge-2026/test-games?wait=30" -H 'Content-Type: application/json' --data-binary @- > "$OUT.json"
    if jq -e '.status == 429' "$OUT.json" >/dev/null 2>&1; then
      sleep "$(jq -r '.retry_after_seconds // 30' "$OUT.json")"; continue
    fi
    if jq -e '.status == 409' "$OUT.json" >/dev/null 2>&1; then sleep 20; continue; fi
    break
  done
  ID=$(jq -r '.test_game_id // empty' "$OUT.json")
  if [ -z "$ID" ]; then echo "game $n vs $OPP: request failed: $(jq -r '.detail // .title' "$OUT.json")"; continue; fi
  if [ "$(jq -r '.status // empty' "$OUT.json")" = "done" ]; then
    jq -r '.log // empty' "$OUT.json" > "$OUT.log"
  else
    while true; do  # the poll answers plain text: "status: ..." then the log
      cg "$API/test-games/$ID?wait=30" > "$OUT.log"
      ST=$(head -1 "$OUT.log" | sed -E 's/^status: *//')
      if [ "$ST" = "done" ] || [ "$ST" = "failed" ]; then break; fi
    done
  fi
  summary "$OUT.log"
done
