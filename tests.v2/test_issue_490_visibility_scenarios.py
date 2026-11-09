"""Contract checks for the CODEX 4 V3-VIS-01 ordinary-player scenario.

These checks validate scenario reproducibility and evidence shape only. They do
not claim live server, PostgreSQL, or canonical acceptance evidence.
"""

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MATRIX = ROOT / "tests.v2" / "issue_490_visibility_case_matrix.json"
SUITE = ROOT / "tests.v2" / "suite_information_fog.json"


class Issue490VisibilityScenarioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.matrix = json.loads(MATRIX.read_text(encoding="utf-8"))
        cls.suite = json.loads(SUITE.read_text(encoding="utf-8"))
        cls.steps = cls.suite["tests"]
        cls.names = {step.get("name", "").lower() for step in cls.steps}
        cls.commands = {step.get("command") for step in cls.steps}

    def test_matrix_is_visibility_owned_and_restart_aware(self):
        self.assertEqual(self.matrix["requirement_id"], "V3-VIS-01")
        self.assertEqual(self.matrix["scenario_id"], "VIS-01-VISIBILITY")
        self.assertEqual(self.matrix["role"], "CODEX 4")
        self.assertTrue(self.matrix["policy"]["ordinary_player_commands_only"])
        self.assertTrue(self.matrix["evidence"]["restart_required"])
        self.assertGreaterEqual(len(self.matrix["cases"]), 5)

    def test_matrix_covers_required_visibility_boundaries(self):
        case_ids = {case["id"] for case in self.matrix["cases"]}
        self.assertEqual(
            case_ids,
            {
                "current-sector",
                "outgoing-adjacent",
                "incoming-only",
                "remote-entity",
                "unvisited-path",
                "restart-readback",
            },
        )
        self.assertIn("engine_events filtered by observer player_id and queried entity",
                      self.matrix["evidence"]["before_after_queries"])

    def test_existing_suite_has_ordinary_player_visibility_commands(self):
        expected = {
            "sector.info",
            "sector.scan",
            "sector.scan.density",
            "port.info",
            "trade.port_info",
            "planet.info",
            "move.pathfind",
            "move.describe_sector",
        }
        self.assertTrue(expected.issubset(self.commands))
        self.assertNotIn("sys.raw_sql_exec", {
            step.get("command") for step in self.steps
            if step.get("user") != "admin"
        })

    def test_existing_suite_names_each_boundary_and_readback(self):
        required_fragments = (
            "current sector information is available",
            "outgoing-only warp makes target sector adjacent",
            "incoming-only warp does not make b adjacent",
            "remote port information is refused",
            "unowned planet details are hidden",
            "pathfind does not use or disclose an undiscovered intermediate sector",
            "refresh restored player sector",
        )
        for fragment in required_fragments:
            self.assertTrue(any(fragment in name for name in self.names), fragment)

    def test_evidence_queries_are_read_only_and_include_events(self):
        sql_steps = [
            step for step in self.steps
            if step.get("command") == "sys.raw_sql_exec"
        ]
        evidence = [
            step for step in sql_steps
            if any(token in step.get("name", "").lower()
                   for token in ("knowledge", "does not discover", "snapshot", "event"))
        ]
        self.assertGreaterEqual(len(evidence), 3)
        self.assertTrue(any("engine_events" in step.get("data", {}).get("sql", "")
                            for step in sql_steps))
        self.assertTrue(any(step.get("data", {}).get("sql", "").lstrip().lower().startswith("select")
                            for step in evidence))


if __name__ == "__main__":
    unittest.main()
