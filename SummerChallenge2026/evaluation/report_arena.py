"""Read-only summary of the candidate evaluation stored by CG Arena."""
import collections
import hashlib
import json
from pathlib import Path
import re
import sqlite3
import sys

ROOT = Path(__file__).resolve().parents[1]
ARENA = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "arena"


def report():
    manifest = json.loads((ARENA / "evaluation_manifest.json").read_text())
    candidate = int(sys.argv[2]) if len(sys.argv) > 2 else manifest["current"]
    connection = sqlite3.connect(f"file:{ARENA / 'cgarena.db'}?mode=ro", uri=True)
    connection.row_factory = sqlite3.Row
    submission = connection.execute("SELECT name, source_code FROM bots WHERE id=?", (candidate,)).fetchone()
    if submission is None:
        raise ValueError(f"Unknown candidate {candidate}")
    rows = connection.execute('''
        SELECT m.id, m.seed, m.replay_path, p."index" AS seat,
               p.rank AS our_rank, q.rank AS their_rank, p.error AS our_error,
               q.error AS their_error, q.bot_id AS opponent_id, b.name AS opponent
        FROM matches m
        JOIN participations p ON p.match_id=m.id AND p.bot_id=?
        JOIN participations q ON q.match_id=m.id AND q.bot_id<>p.bot_id
        JOIN bots b ON b.id=q.bot_id
        ORDER BY m.id
    ''', (candidate,)).fetchall()
    groups = collections.defaultdict(list)
    for row in rows:
        groups[row["opponent"]].append(row)
    summaries = {}
    example_losses = []
    all_times = []
    all_rejections = collections.Counter()
    for opponent, matches in groups.items():
        wins = draws = losses = warnings = affected = our_errors = their_errors = 0
        clean_wins = clean_draws = clean_losses = 0
        our_scores, their_scores = [], []
        seats = collections.Counter()
        for match in matches:
            seats[match["seat"]] += 1
            wins += match["our_rank"] < match["their_rank"]
            draws += match["our_rank"] == match["their_rank"]
            losses += match["our_rank"] > match["their_rank"]
            our_errors += match["our_error"]
            their_errors += match["their_error"]
            if not match["our_error"] and not match["their_error"]:
                clean_wins += match["our_rank"] < match["their_rank"]
                clean_draws += match["our_rank"] == match["their_rank"]
                clean_losses += match["our_rank"] > match["their_rank"]
            replay = json.loads((ARENA / match["replay_path"]).read_text())
            seat = match["seat"]
            our_scores.append(replay["scores"][str(seat)])
            their_scores.append(replay["scores"][str(1 - seat)])
            summary = "\n".join(line or "" for line in replay["summaries"])
            count = len(re.findall(rf"\${seat} Cannot disrupt region\d+\. It contains a town", summary))
            warnings += count
            affected += count > 0
            for line in summary.splitlines():
                if f"${seat} " in line and "¤RED¤" in line:
                    normalized = re.sub(r"region\d+", "region#", line)
                    all_rejections[normalized] += 1
            logs = "\n".join(line or "" for line in replay["errors"].get(str(seat), []))
            all_times.extend(float(value) for value in re.findall(r"decision_ms=([\d.]+)", logs))
            if match["our_rank"] > match["their_rank"] and len(example_losses) < 5:
                example_losses.append({"match": match["id"], "seed": match["seed"],
                                       "seat": seat, "score": our_scores[-1],
                                       "opponent_score": their_scores[-1],
                                       "town_disruption_rejections": count,
                                       "replay": match["replay_path"]})
        summaries[opponent] = {
            "matches": len(matches), "wins": wins, "draws": draws, "losses": losses,
            "win_rate": wins / len(matches), "unique_seeds": len({m["seed"] for m in matches}),
            "matches_by_seat": dict(seats), "candidate_errors": our_errors,
            "opponent_errors": their_errors, "town_disruption_rejections": warnings,
            "error_free_results": {"wins": clean_wins, "draws": clean_draws, "losses": clean_losses},
            "matches_with_town_rejections": affected,
            "mean_candidate_score": sum(our_scores) / len(matches),
            "mean_opponent_score": sum(their_scores) / len(matches),
        }
    all_times.sort()
    source_hash = hashlib.sha256((ROOT / "main.cpp").read_bytes()).hexdigest()
    candidate_hash = hashlib.sha256(submission["source_code"].encode()).hexdigest()
    result = {
        "candidate_id": candidate, "candidate_name": submission["name"],
        "candidate_source_unchanged": source_hash == candidate_hash,
        "candidate_source_sha256": candidate_hash, "total_matches": len(rows),
        "league": 3, "referee_commit": "2c19cc030266b3bf53efc946dc30c211c6c3eefa",
        "opponents": summaries,
        "decision_ms": {"samples": len(all_times),
                        "p95": all_times[int(0.95 * len(all_times))] if all_times else None,
                        "max": max(all_times) if all_times else None},
        "candidate_rejections": dict(all_rejections), "example_losses": example_losses,
    }
    connection.close()
    payload = json.dumps(result, indent=2) + "\n"
    (ARENA / "evaluation_report.json").write_text(payload)
    (ARENA / f"evaluation_report_{candidate}.json").write_text(payload)
    return result


if __name__ == "__main__":
    print(json.dumps(report(), indent=2))
