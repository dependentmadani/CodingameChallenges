"""Fast regression tests for the tuning objective and failure policy."""
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tuning"))
import arena
import spsa


class ArenaTests(unittest.TestCase):
    def compare(self, results, require_clean=True):
        with mock.patch.object(arena, "one", side_effect=results):
            return arena.compare("a", "b", len(results) // 2, 1, 1, 0,
                                 require_clean, 0.8, 500.0)

    def test_incomplete_or_forfeited_evaluation_has_no_score(self):
        result = self.compare([(10, False), (20, False),
                               (-10**6, True), (5, False)])
        self.assertIn("error", result)
        self.assertNotIn("score", result)
        self.assertEqual(result["failures"], 1)

    def test_bounded_objective_follows_wins_not_one_blowout(self):
        result = self.compare([(100000, False), (-1, False),
                               (-1, False), (-1, False)], require_clean=False)
        self.assertLess(result["objective"], 0)
        self.assertEqual((result["wins"], result["losses"]), (1, 3))

    def test_score_contains_uncertainty_and_raw_margin(self):
        result = self.compare([(100, False), (50, False),
                               (-20, False), (-10, False)], require_clean=False)
        self.assertIn("objective_se", result)
        self.assertEqual(result["paired_mean"], 30.0)


class SpsaTests(unittest.TestCase):
    def test_structural_integer_parameters_are_not_in_spsa_groups(self):
        for group in ("search", "attack", "planner", "all"):
            selected = [name for name, spec in spsa.SPEC.items()
                        if spec[3] == "f" and spsa.in_group(name, group)]
            self.assertTrue(selected)
            self.assertNotIn("P_TREE_DEPTH", selected)
            self.assertTrue(all(spsa.SPEC[name][3] == "f" for name in selected))


if __name__ == "__main__":
    unittest.main()
