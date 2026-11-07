"""Differential tests of the bot's rollout simulator against test_bot.Board."""
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
# test_bot is also a CLI: preserve unittest arguments when importing its model.
saved_argv = sys.argv[:]
try:
    sys.argv = [sys.argv[0]]
    import test_bot
finally:
    sys.argv = saved_argv


class SearchSimulationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="summer-search-tests-")
        cls.probe = Path(cls.temp.name) / "probe"
        flags = ["-O1", "-g", "-fsanitize=address,undefined"] if os.environ.get(
            "SEARCH_SANITIZE") else ["-O2"]
        subprocess.run(["clang++", "-std=c++17", *flags,
                        str(ROOT / "tests/search_probe.cpp"), "-o", str(cls.probe)],
                       check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def compare(self, board, actions, player=0, turn=1):
        encoded = []
        for action in (actions[player], actions[1 - player]):
            cells, attacks = board.validate(action)
            encoded.append(f"{len(cells)} {attacks[0] if attacks else -1} "
                           + " ".join(map(str, sorted(cells))))
        data = (board.initial_input(player) + board.turn_input(player) +
                "\n".join(encoded) + f"\n{turn}\n")
        result = subprocess.run([str(self.probe)], input=data, capture_output=True,
                                text=True, timeout=5, check=True)
        values = list(map(int, result.stdout.split()))
        board.resolve(actions)
        rate = board.income()
        self.assertEqual(values[:3], [
            board.scores[player] - board.scores[1 - player],
            rate[player] - rate[1 - player],
            int(turn >= 100 or not board.paths(potential=True)),
        ])
        forecast = [0] * 6
        for _, path in board.paths():
            net = sum((board.tracks.get(c) == player) - (board.tracks.get(c) == 1 - player)
                      for c in path if c not in board.town_cells)
            lifetime = min((4 - board.instability[board.regions[c]] for c in path
                            if board.regions[c] not in board.town_regions), default=5)
            forecast[lifetime] += net
        self.assertEqual(values[3:9], forecast)
        expected = []
        for c in range(board.size):
            expected.extend((board.tracks.get(c, -1), int(board.blocked(c))))
        self.assertEqual(values[9:], expected)

    def test_collision_and_two_disruptions_before_scoring(self):
        for player in (0, 1):
            b = test_bot.Board(5, 1, {7: (0, [31]), 31: (4, [])})
            b.tracks = {1: 0, 3: 1}
            b.instability[2] = 2
            self.compare(b, ["PLACE_TRACKS 2 0;DISRUPT 2"] * 2, player)

    def test_income_path_ties_and_shared_tracks(self):
        for player in (0, 1):
            b = test_bot.Board(3, 3, {7: (6, [31, 50]), 31: (2, []), 50: (0, [])})
            b.tracks = {3: 1, 1: 1, 7: 0, 8: 0, 5: 0}
            b.instability[3] = 3
            self.compare(b, ["DISRUPT 3", "WAIT"], player)

    def test_randomized_simultaneous_turns(self):
        rng = random.Random(20260914)
        for seed in range(16):
            b = test_bot.generated_board(seed)
            for c in range(b.size):
                if c not in b.town_cells and rng.random() < 0.42:
                    b.tracks[c] = rng.randrange(3)
            for r in b.instability:
                if r not in b.town_regions:
                    b.instability[r] = rng.randrange(4)
            b.scores = [rng.randrange(500), rng.randrange(500)]
            for _ in range(4):
                actions = []
                for player in (0, 1):
                    budget = 3
                    commands = []
                    cells = list(range(b.size))
                    rng.shuffle(cells)
                    for c in cells:
                        cost = b.terrain[c] + 1
                        if (c not in b.town_cells and c not in b.tracks and
                                not b.blocked(c) and cost <= budget):
                            commands.append(f"PLACE_TRACKS {c % b.width} {c // b.width}")
                            budget -= cost
                    targets = [r for r in b.instability
                               if r not in b.town_regions and r not in b.inked]
                    if targets and rng.random() < 0.85:
                        commands.append(f"DISRUPT {rng.choice(targets)}")
                    actions.append(";".join(commands) or "WAIT")
                self.compare(b, actions, seed % 2)
                if not b.paths(potential=True):
                    break

    def test_final_turn_still_scores_before_termination(self):
        for player in (0, 1):
            b = test_bot.Board(5, 1, {7: (0, [31]), 31: (4, [])})
            b.tracks = {1: 0, 3: 1}
            b.scores = [19, 17]
            self.compare(b, ["PLACE_TRACKS 2 0", "WAIT"], player, turn=100)

    def test_cutting_last_possible_connection_ends_game(self):
        for player in (0, 1):
            b = test_bot.Board(5, 1, {7: (0, [31]), 31: (4, [])})
            b.tracks = {1: 0, 2: 1, 3: 1}
            b.instability[2] = 3
            b.scores = [19, 23]
            self.compare(b, ["DISRUPT 2", "WAIT"], player)

    def test_maximum_board_dense_directed_requests(self):
        towns = {i: (i * 50, [j for j in range(12) if j != i]) for i in range(12)}
        for player in (0, 1):
            b = test_bot.Board(30, 20, towns, regions=[c // 10 for c in range(600)])
            b.tracks = {c: (c % 3) for c in range(600) if c not in b.town_cells}
            b.instability[11] = 3
            b.scores = [100000, 99999]
            self.compare(b, ["DISRUPT 11", "WAIT"], player)

    def test_disruption_horizon_distinguishes_town_protection(self):
        for player in (0, 1):
            for unstable in range(4):
                for immune in (False, True):
                    regions = [11, 11, 11 if immune else 12, 13, 13]
                    b = test_bot.Board(5, 1, {7: (0, [31]), 31: (4, [])}, regions=regions)
                    b.tracks = {1: 0, 2: 0, 3: 1}
                    if not immune:
                        b.instability[12] = unstable
                    self.compare(b, ["WAIT", "WAIT"], player)


if __name__ == "__main__":
    unittest.main()
