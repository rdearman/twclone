#!/usr/bin/env python3
"""Offline checks for the CODEX 4 issue #490 scenario contract."""

import json
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests.v2"))

import run_issue_490_ai_qa as harness


class Issue490ScenarioContractTests(unittest.TestCase):
    def test_catalog_is_requirement_scoped_and_executable(self):
        catalog = harness.load_catalog()
        self.assertEqual(catalog["requirement_id"], "V3-PLAN-01")
        self.assertTrue(catalog["policy"]["ordinary_player_commands_only"])
        self.assertTrue(catalog["policy"]["response_capture_required"])
        self.assertTrue(catalog["policy"]["event_and_database_snapshots_required"])
        self.assertGreaterEqual(len(catalog["scenarios"]), 8)
        self.assertEqual(harness.next_unblocked_scenarios(catalog), ["PLAN-01-LANDING"])

    def test_default_selection_does_not_run_deferred_or_decision_gated_scenarios(self):
        catalog = harness.load_catalog()
        statuses = {item["id"]: item["execution_status"] for item in catalog["scenarios"]}
        self.assertEqual(statuses["PLAN-01-LANDING"], "next-unblocked")
        self.assertTrue(all(
            statuses[item] in {"deferred", "decision-gated"}
            for item in statuses if item != "PLAN-01-LANDING"
        ))
        with self.assertRaises(SystemExit):
            harness.main([
                "--qa-database", "twclone_v3qa_codex4",
                "--capture", "/tmp/issue-490-contract.jsonl",
                "--scenario", "INS-01-INSURANCE",
            ])

    def test_requested_gameplay_families_are_present(self):
        with harness.CATALOG.open(encoding="utf-8") as stream:
            catalog = json.load(stream)
        ids = {scenario["id"] for scenario in catalog["scenarios"]}
        self.assertEqual(
            ids,
            {
                "PLAN-01-LANDING",
                "SHIP-02-OWNERSHIP",
                "ECO-01-TRADE",
                "ECO-02-BANK-INTEREST",
                "VIS-01-VISIBILITY",
                "HAZ-01-ENTRY",
                "PLAN-02-PLANET-LIFECYCLE",
                "INS-01-INSURANCE",
            },
        )

    def test_non_loopback_is_rejected_before_network_use(self):
        self.assertFalse(harness.loopback("192.0.2.1"))

    def test_database_identity_must_be_disposable(self):
        harness.validate_qa_database("twclone_v3qa_codex4")
        with self.assertRaises(SystemExit):
            harness.validate_qa_database("twclone_production")

    def test_fixture_sql_rejects_schema_and_cluster_mutation(self):
        harness.validate_fixture_sql({
            "command": "sys.raw_sql_exec",
            "user": "admin",
            "data": {"sql": "SELECT player_id FROM players"},
        })
        with self.assertRaises(SystemExit):
            harness.validate_fixture_sql({
                "command": "sys.raw_sql_exec",
                "user": "admin",
                "data": {"sql": "DROP TABLE players"},
            })
        with self.assertRaises(SystemExit):
            harness.validate_fixture_sql({
                "command": "sys.raw_sql_exec",
                "user": "ordinary_player",
                "data": {"sql": "SELECT 1"},
            })

    def test_evidence_steps_are_read_only_admin_queries(self):
        harness.validate_evidence_step({
            "name": "Evidence snapshot before",
            "command": "sys.raw_sql_exec",
            "user": "admin",
            "data": {"sql": "SELECT player_id FROM players"},
        })
        with self.assertRaises(SystemExit):
            harness.validate_evidence_step({
                "name": "Evidence snapshot after",
                "command": "planet.land",
                "user": "ordinary_player",
                "data": {"planet_id": 1},
            })
        with self.assertRaises(SystemExit):
            harness.validate_evidence_step({
                "name": "Event persistence evidence",
                "command": "sys.raw_sql_exec",
                "user": "admin",
                "data": {"sql": "UPDATE engine_events SET type='bad'"},
            })

    def test_selected_landing_suite_has_database_and_event_evidence(self):
        harness.validate_suite_evidence("tests.v2/suite_planet_landing_defense.json")

    def test_fixture_sql_rejects_row_deletion(self):
        with self.assertRaises(SystemExit):
            harness.validate_fixture_sql({
                "command": "sys.raw_sql_exec",
                "user": "admin",
                "data": {"sql": "DELETE FROM engine_events"},
            })

    def test_infrastructure_failure_is_captured_as_structured_blocker(self):
        with TemporaryDirectory() as directory:
            capture = Path(directory) / "responses.jsonl"
            error = PermissionError(1, "Operation not permitted")
            harness.record_runner_failure(
                capture, scenario_id="PLAN-01-LANDING", host="127.0.0.1",
                port=12483, error=error,
            )
            record = json.loads(capture.read_text(encoding="utf-8"))
            self.assertEqual(record["kind"], "runner_failure")
            self.assertEqual(record["failure_class"], "infrastructure")
            self.assertEqual(record["scenario"], "PLAN-01-LANDING")
            self.assertNotIn("session", capture.read_text(encoding="utf-8").lower())

    def test_planet_landing_suite_contains_redacted_evidence_snapshots(self):
        suite_path = ROOT / "tests.v2" / "suite_planet_landing_defense.json"
        with suite_path.open(encoding="utf-8") as stream:
            suite = json.load(stream)

        names = {step.get("name") for step in suite["tests"]}
        self.assertIn("Evidence snapshot before owner landing", names)
        self.assertIn("Evidence snapshot after owner landing", names)

        snapshots = [
            step for step in suite["tests"]
            if step.get("name", "").startswith("Evidence snapshot")
        ]
        self.assertEqual(len(snapshots), 2)
        for step in snapshots:
            self.assertEqual(step["command"], "sys.raw_sql_exec")
            self.assertEqual(step["user"], "admin")
            sql = step["data"]["sql"].lstrip().upper()
            self.assertTrue(sql.startswith("SELECT "))
            self.assertNotIn("; UPDATE ", sql)
            self.assertNotIn("; INSERT ", sql)
            self.assertNotIn("; DELETE ", sql)

        after = next(
            step for step in snapshots
            if step["name"] == "Evidence snapshot after owner landing"
        )
        turn_assertions = [
            assertion for assertion in after["asserts"]
            if assertion["path"] == "data.rows.0.3"
        ]
        self.assertEqual(len(turn_assertions), 1)
        self.assertIn("landing_owner_turns_before", turn_assertions[0]["value"]["sql_int"])

    def test_catalog_keeps_all_required_evidence_fields(self):
        with harness.CATALOG.open(encoding="utf-8") as stream:
            catalog = json.load(stream)
        for scenario in catalog["scenarios"]:
            harness.validate_scenario_contract(scenario)
            self.assertTrue(scenario["requirement_id"])
            self.assertGreaterEqual(len(scenario["required_cases"]), 2)
            self.assertGreaterEqual(len(scenario["before_after_queries"]), 2)
            self.assertIn(scenario["id"], harness.SUITES)

    def test_plan01_case_matrix_does_not_overclaim_live_coverage(self):
        matrix_path = ROOT / "tests.v2" / "issue_490_plan01_case_matrix.json"
        with matrix_path.open(encoding="utf-8") as stream:
            matrix = json.load(stream)
        self.assertEqual(matrix["requirement_id"], "V3-PLAN-01")
        self.assertEqual(matrix["scenario_id"], "PLAN-01-LANDING")
        with (ROOT / "tests.v2" / "suite_planet_landing_defense.json").open(
            encoding="utf-8"
        ) as stream:
            suite_names = {step.get("name") for step in json.load(stream)["tests"]}
        covered = {case["id"] for case in matrix["cases"] if case["acceptance_status"] == "covered"}
        not_covered = {case["id"] for case in matrix["cases"] if case["acceptance_status"] == "not-covered"}
        self.assertEqual(
            covered,
            {"unauthorized", "defended refusal", "valid landing"},
        )
        self.assertEqual(
            not_covered,
            {"malformed", "remote", "retry", "restart readback"},
        )
        for case in matrix["cases"]:
            if case["acceptance_status"] == "covered":
                self.assertTrue(
                    set(case["matching_test_names"]).issubset(suite_names),
                    case["id"],
                )
            else:
                self.assertNotIn("matching_test_names", case)

    def test_each_family_has_replayable_player_and_state_contract(self):
        with harness.CATALOG.open(encoding="utf-8") as stream:
            catalog = json.load(stream)
        for scenario in catalog["scenarios"]:
            self.assertGreaterEqual(len(scenario["ordinary_player_flow"]), 2)
            self.assertGreaterEqual(len(scenario["refusal_invariants"]), 2)
            self.assertGreaterEqual(len(scenario["success_invariants"]), 1)
            self.assertGreaterEqual(len(scenario["event_filters"]), 1)
            self.assertIsInstance(scenario["restart_required"], bool)

    def test_scenario_gameplay_commands_cannot_smuggle_admin_sql(self):
        with harness.CATALOG.open(encoding="utf-8") as stream:
            catalog = json.load(stream)
        for scenario in catalog["scenarios"]:
            self.assertNotIn("sys.raw_sql_exec", scenario["gameplay_commands"])
            self.assertTrue(any(
                "event" in query.lower()
                for query in scenario["before_after_queries"]
            ))


if __name__ == "__main__":
    unittest.main()
