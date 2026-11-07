"""Independent Bronze rule checks and a small synthetic arena.

python3 test_bot.py /path/to/bot
python3 test_bot.py /path/to/bot --arena
"""
import collections
import heapq
import random
import selectors
import subprocess
import sys
import time
import unittest

BOT = sys.argv.pop(1) if len(sys.argv) > 1 else "./bot"


class Board:
    def __init__(self, width, height, towns, terrain=None, regions=None):
        self.width, self.height = width, height
        self.size = width * height
        self.towns = towns  # id -> (cell, requested town ids)
        self.town_cells = {cell for cell, _ in towns.values()}
        self.terrain = terrain or [0] * self.size
        self.regions = regions or list(range(self.size))
        self.town_regions = {self.regions[cell] for cell in self.town_cells}
        self.instability = {r: 0 for r in self.regions}
        self.inked = set()
        self.tracks = {}
        self.scores = [0, 0]
        self.neighbors = []
        for cell in range(self.size):
            x, y = cell % width, cell // width
            self.neighbors.append([
                ny * width + nx
                for nx, ny in ((x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y))
                if 0 <= nx < width and 0 <= ny < height
            ])

    def blocked(self, cell):
        return self.regions[cell] in self.inked

    def initial_input(self, player):
        lines = [str(player), f"{self.width} {self.height}"]
        lines += [f"{r} {t}" for r, t in zip(self.regions, self.terrain)]
        lines += [str(len(self.towns))]
        for town_id, (cell, desired) in self.towns.items():
            requests = ",".join(map(str, desired)) or "x"
            lines.append(f"{town_id} {cell % self.width} {cell // self.width} {requests}")
        return "\n".join(lines) + "\n"

    def paths(self, potential=False):
        result = []
        for town_id, (start, desired) in self.towns.items():
            if self.blocked(start):
                continue
            parents = {start: None}
            queue = collections.deque([start])
            while queue:
                cell = queue.popleft()
                for other in self.neighbors[cell]:
                    if other in parents or self.blocked(other):
                        continue
                    if potential or other in self.town_cells or other in self.tracks:
                        parents[other] = cell
                        queue.append(other)
            for target_id in desired:
                cell = self.towns[target_id][0]
                if cell not in parents:
                    continue
                path = []
                while cell is not None:
                    path.append(cell)
                    cell = parents[cell]
                result.append((f"{town_id}-{target_id}", path[::-1]))
        return result

    def income(self):
        income = [0, 0]
        for _, path in self.paths():
            for cell in path:
                owner = self.tracks.get(cell)
                if cell not in self.town_cells and owner in (0, 1):
                    income[owner] += 1
        return income

    def turn_input(self, player):
        membership = collections.defaultdict(list)
        for name, path in self.paths():
            for cell in path:
                membership[cell].append(name)
        lines = [f"{self.scores[player]} {self.scores[1 - player]}"]
        for cell, region in enumerate(self.regions):
            active = ",".join(membership[cell]) or "x"
            lines.append(f"{self.tracks.get(cell, -1)} {self.instability[region]} "
                         f"{int(region in self.inked)} {active}")
        return "\n".join(lines) + "\n"

    def validate(self, line):
        assert line, "Missing action"
        placed, disrupted = set(), []
        paint = 3
        for action in line.split(";"):
            parts = action.split()
            if parts == ["WAIT"]:
                continue
            if parts[0] == "PLACE_TRACKS":
                assert len(parts) == 3, action
                x, y = map(int, parts[1:])
                assert 0 <= x < self.width and 0 <= y < self.height, action
                cell = y * self.width + x
                assert cell not in self.town_cells and cell not in self.tracks, action
                assert not self.blocked(cell) and cell not in placed, action
                paint -= self.terrain[cell] + 1
                assert paint >= 0, "Exceeded three paint"
                placed.add(cell)
            elif parts[0] == "DISRUPT":
                assert len(parts) == 2, action
                region = int(parts[1])
                assert region in self.instability and region not in self.inked, action
                assert region not in self.town_regions, "Cannot disrupt a region containing a town"
                disrupted.append(region)
                assert len(disrupted) <= 1, "Exceeded one disruption"
            else:
                raise AssertionError(f"Unexpected action: {action}")
        return placed, disrupted

    def resolve(self, actions):
        # Both players choose on the same state. Collisions create neutral tracks.
        parsed = [self.validate(action) for action in actions]
        for cell in parsed[0][0] | parsed[1][0]:
            self.tracks[cell] = 2 if cell in parsed[0][0] & parsed[1][0] else (
                0 if cell in parsed[0][0] else 1)
        for _, disruptions in parsed:
            for region in disruptions:
                self.instability[region] += 1
        for region, instability in self.instability.items():
            if instability >= 4 and region not in self.town_regions:
                self.inked.add(region)
        self.tracks = {cell: owner for cell, owner in self.tracks.items() if not self.blocked(cell)}
        income = self.income()
        self.scores = [score + gained for score, gained in zip(self.scores, income)]


class Bot:
    def __init__(self, board, player=0):
        self.player = player
        self.process = subprocess.Popen(
            [BOT], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, bufsize=1,
        )
        self.ready = selectors.DefaultSelector()
        self.ready.register(self.process.stdout, selectors.EVENT_READ)
        self.process.stdin.write(board.initial_input(player))
        self.latencies = []

    def action(self, board):
        data = board.turn_input(self.player)
        started = time.perf_counter()
        self.process.stdin.write(data)
        self.process.stdin.flush()
        assert self.ready.select(timeout=2), "Bot did not flush an action"
        line = self.process.stdout.readline().strip()
        self.latencies.append((time.perf_counter() - started) * 1000)
        board.validate(line)
        return line

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=2)
            assert self.process.returncode == 0, "Bot crashed"
            assert self.process.stdout.read() == "", "Extra output after EOF"
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()
            self.process.stdout.close()
            self.ready.close()


def baseline(board, player, disrupt=True):
    """Cheap requested-route builder, with optional raw-track-count sabotage."""
    best = None
    for start, desired in board.towns.values():
        if board.blocked(start):
            continue
        distance, parent = {start: (0, 0)}, {start: None}
        queue = [(0, 0, start)]
        while queue:
            cost, length, cell = heapq.heappop(queue)
            if distance[cell] != (cost, length):
                continue
            for other in board.neighbors[cell]:
                if board.blocked(other):
                    continue
                step = 0 if other in board.town_cells or other in board.tracks else board.terrain[other] + 1
                candidate = (cost + step, length + 1)
                if candidate < distance.get(other, (10**9, 10**9)):
                    distance[other] = candidate
                    parent[other] = cell
                    heapq.heappush(queue, (*candidate, other))
        for target in desired:
            cell = board.towns[target][0]
            if cell not in distance or distance[cell][0] == 0:
                continue
            path = []
            while cell is not None:
                if cell not in board.tracks and cell not in board.town_cells:
                    path.append(cell)
                cell = parent[cell]
            value = distance[board.towns[target][0]]
            if best is None or value < best[0]:
                best = (value, path)
    actions, built = [], set()
    if best:
        budget = 3
        for cell in sorted(best[1], key=lambda c: (-board.terrain[c], c)):
            cost = board.terrain[cell] + 1
            if cost <= budget:
                actions.append(f"PLACE_TRACKS {cell % board.width} {cell // board.width}")
                built.add(cell)
                budget -= cost
    if disrupt:
        counts = collections.defaultdict(float)
        for cell, owner in board.tracks.items():
            if owner == 1 - player:
                counts[board.regions[cell]] += 1
            elif owner == player:
                counts[board.regions[cell]] -= 1.5
        candidates = [(value / (4 - board.instability[region]), region)
                      for region, value in counts.items()
                      if value > 0 and region not in board.inked
                      and region not in board.town_regions
                      and all(board.regions[cell] != region for cell in built)]
        if candidates:
            actions.append(f"DISRUPT {max(candidates)[1]}")
    return ";".join(actions) or "WAIT"


def generated_board(seed):
    rng = random.Random(seed)
    width, height = rng.randint(21, 30), rng.randint(14, 20)
    columns = (width + 2) // 3
    regions = [(y // 2) * columns + x // 3 for y in range(height) for x in range(width)]
    terrain = rng.choices([0, 1, 2], weights=[7, 2, 1], k=width * height)
    selected = []
    for region in rng.sample(sorted(set(regions)), len(set(regions))):
        if all(abs(region % columns - other % columns) +
               abs(region // columns - other // columns) > 1 for other in selected):
            selected.append(region)
        if len(selected) == 12:
            break
    selected = selected[:rng.randint(4, len(selected))]
    towns = {}
    for i, region in enumerate(selected):
        cell = rng.choice([c for c, r in enumerate(regions) if r == region])
        terrain[cell] = 0
        towns[i * 10 + 7] = (cell, [((i + 1) % len(selected)) * 10 + 7])
    ids = list(towns)
    for i, source in enumerate(ids):
        for target in ids[i + 1:]:
            if target not in towns[source][1] and source not in towns[target][1] and rng.random() < 0.2:
                towns[source][1].append(target)
    return Board(width, height, towns, terrain, regions)


class BotTests(unittest.TestCase):
    def start(self, board, player=0):
        bot = Bot(board, player)
        self.addCleanup(bot.close)
        return bot

    def test_empty_and_truncated_input(self):
        for data in ("", "0\n", "0\n1 1\n0 0\n0\n", "0\n1 1\n0 0\n0\n0 0\n"):
            result = subprocess.run([BOT], input=data, text=True, capture_output=True, timeout=2)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")

    def test_builds_scoring_connection_for_either_player(self):
        for player in (0, 1):
            board = Board(5, 1, {10: (0, [42]), 42: (4, [])})
            bot = self.start(board, player)
            actions = ["WAIT", "WAIT"]
            actions[player] = bot.action(board)
            board.resolve(actions)
            self.assertEqual(board.scores[player], 3)

    def test_cheap_terrain_detour(self):
        board = Board(5, 2, {0: (0, [1]), 1: (4, [])}, [0, 2, 2, 2, 0] + [0] * 5)
        bot = self.start(board)
        for _ in range(2):
            board.resolve([bot.action(board), "WAIT"])
        self.assertGreater(board.scores[0], 0)
        self.assertTrue(all(board.terrain[cell] == 0 for cell in board.tracks))

    def test_build_and_disrupt_in_same_turn(self):
        board = Board(5, 3, {0: (0, [1]), 1: (4, []), 2: (10, [3]), 3: (14, [])})
        board.tracks = {1: 1, 2: 1, 3: 1}
        board.instability[2] = 3
        bot = self.start(board)
        action = bot.action(board)
        self.assertIn("PLACE_TRACKS", action)
        self.assertIn("DISRUPT", action)
        board.resolve([action, "WAIT"])
        self.assertGreater(board.scores[0], 0)
        self.assertEqual(board.scores[1], 0)

    def test_does_not_destroy_own_profitable_connection(self):
        board = Board(6, 1, {0: (0, [1]), 1: (5, [])})
        board.tracks = {1: 0, 2: 0, 3: 0, 4: 1}
        board.instability[4] = 3
        bot = self.start(board)
        self.assertEqual(bot.action(board), "WAIT")

    def test_town_region_immunity_in_simulator(self):
        board = Board(5, 1, {0: (0, [1]), 1: (4, [])}, regions=[10, 10, 23, 91, 91])
        with self.assertRaisesRegex(AssertionError, "containing a town"):
            board.validate("DISRUPT 10")
        board.tracks = {1: 1}
        board.instability[10] = 4
        board.resolve(["WAIT", "WAIT"])
        self.assertNotIn(10, board.inked)
        self.assertEqual(board.tracks[1], 1)

    def test_skips_whole_town_region_not_just_town_tile(self):
        for player in (0, 1):
            board = Board(5, 1, {0: (0, [1]), 1: (4, [])},
                          regions=[10, 10, 23, 91, 91])
            board.tracks = {1: 1 - player, 2: 1 - player, 3: 1 - player}
            bot = self.start(board, player)
            self.assertEqual(bot.action(board), "DISRUPT 23")

    def test_all_enemy_tracks_in_immune_regions(self):
        board = Board(5, 1, {0: (0, [1]), 1: (4, [])}, regions=[10, 10, 10, 91, 91])
        board.tracks = {1: 1, 2: 1, 3: 1}
        bot = self.start(board)
        self.assertEqual(bot.action(board), "WAIT")

    def test_disruption_reroutes_income_to_us(self):
        board = Board(5, 2, {0: (0, [1]), 1: (4, [])})
        board.tracks = {1: 1, 2: 1, 3: 1, **{c: 0 for c in range(5, 10)}}
        board.instability[2] = 3
        bot = self.start(board)
        self.assertEqual(board.income(), [0, 3])
        board.resolve([bot.action(board), "WAIT"])
        # Equal-length detours still prefer east at the requesting town, so the
        # surviving enemy pieces at either end remain on the active path.
        self.assertEqual(board.income(), [3, 2])

    def test_north_east_south_west_tie_break(self):
        board = Board(3, 3, {10: (6, [20]), 20: (2, [])})
        board.tracks = {3: 1, 0: 1, 1: 1, 7: 0, 8: 0, 5: 0}
        board.instability[3] = 3
        bot = self.start(board)
        self.assertEqual(board.income(), [0, 3])
        board.resolve([bot.action(board), "WAIT"])
        self.assertEqual(board.income(), [3, 0])

    def test_no_forced_losing_shutdown(self):
        board = Board(5, 1, {0: (0, [1]), 1: (4, [])})
        board.tracks = {1: 1, 2: 1, 3: 1}
        board.instability[2] = 3
        board.scores = [0, 20]
        bot = self.start(board)
        self.assertNotIn("DISRUPT 2", bot.action(board))

    def test_neutral_collision_and_disruption_before_scoring(self):
        board = Board(5, 1, {0: (0, [1]), 1: (4, [])})
        board.tracks = {1: 0, 3: 1}
        board.resolve(["PLACE_TRACKS 2 0", "PLACE_TRACKS 2 0"])
        self.assertEqual(board.tracks[2], 2)
        self.assertEqual(board.scores, [1, 1])
        board.instability[2] = 3
        board.resolve(["DISRUPT 2", "WAIT"])
        self.assertEqual(board.scores, [1, 1])

    def test_ignores_inked_cells_and_nonsequential_region_ids(self):
        board = Board(5, 2, {7: (0, [99]), 99: (4, [])},
                      regions=[10, 23, 23, 23, 91, 101, 102, 103, 104, 105])
        board.inked.add(23)
        board.instability[23] = 4
        bot = self.start(board)
        for _ in range(2):
            board.resolve([bot.action(board), "WAIT"])
        self.assertGreater(board.scores[0], 0)

    def test_last_turn_does_not_start_unfinishable_route(self):
        board = Board(6, 1, {0: (0, [1]), 1: (5, [])})
        bot = self.start(board)
        # Replay an empty state to advance the bot's turn counter.
        for _ in range(99):
            bot.action(board)
        self.assertEqual(bot.action(board), "WAIT")

    def test_generated_games_have_legal_actions_and_positive_score(self):
        for seed in range(6):
            board = generated_board(seed)
            bot = Bot(board, seed % 2)
            try:
                for _ in range(25):
                    actions = ["WAIT", "WAIT"]
                    actions[bot.player] = bot.action(board)
                    board.resolve(actions)
                self.assertGreater(board.scores[bot.player], 0, f"seed={seed}")
            finally:
                bot.close()


def arena():
    times = []
    for sabotage in (False, True):
        wins = draws = losses = 0
        margins = []
        for seed in range(12):
            for player in (0, 1):
                board = generated_board(seed)
                bot = Bot(board, player)
                try:
                    for _ in range(100):
                        actions = ["WAIT", "WAIT"]
                        actions[player] = bot.action(board)
                        actions[1 - player] = baseline(board, 1 - player, sabotage)
                        board.resolve(actions)
                        if not board.paths(potential=True):
                            break
                    margin = board.scores[player] - board.scores[1 - player]
                    margins.append(margin)
                    wins += margin > 0
                    draws += margin == 0
                    losses += margin < 0
                    times.extend(bot.latencies[1:])  # First turn includes process startup.
                finally:
                    bot.close()
        print(f"Baseline sabotage={sabotage}: {wins} wins / {draws} draws / {losses} losses; "
              f"mean score margin {sum(margins) / len(margins):.1f}", flush=True)
    times.sort()
    print(f"Response time, including local IPC: p95={times[int(0.95 * len(times))]:.2f} ms; "
          f"max={max(times):.2f} ms", flush=True)


if __name__ == "__main__":
    if "--arena" in sys.argv:
        arena()
    else:
        unittest.main()
