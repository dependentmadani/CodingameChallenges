#!/bin/bash
# Submit a bot file to Fall Challenge 2026 and show the rank before and after.
#   cg_api/submit.sh bot_v1e.cpp
set -eu
FILE=${1:?usage: submit.sh BOT.cpp}
API=https://www.codingame.com/api/v1
cg() { curl -s --max-time 120 -H "Authorization: Bearer $(cat ~/.config/codingame/token)" "$@"; }
echo "before: $(cg $API/challenges/fall-challenge-2026/progress | jq -c '{submission_id, percentage, score, rank}')"
jq -n --rawfile code "$FILE" '{language: "C++", code: $code}' \
  | cg -X POST $API/challenges/fall-challenge-2026/submissions -H 'Content-Type: application/json' --data-binary @- \
  | jq -c '{submission_id, title, detail}'
echo "submitted $FILE; ranking games take about an hour to reach 100%"
