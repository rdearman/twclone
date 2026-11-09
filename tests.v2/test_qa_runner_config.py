"""Regression checks for configurable, isolated V3 integration runners."""

import os
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests.v2"))

import json_runner
import rig_db
import run_suites_all
import run_regression_instance


class RunnerConfigurationTests(unittest.TestCase):
    def test_failed_case_records_assertions_and_reason(self):
        runner = json_runner.JsonSuiteRunner("127.0.0.1", 1)
        runner.active_suite = "sample.json"
        runner.case_results = []
        def fail_with_response(_test):
            runner._current_response = {"status": "error", "error": {"code": 401}}
            runner._current_failure_reason = "Status: error != ok"
            return False

        with patch.object(runner, "_run_test_impl", side_effect=fail_with_response):
            passed = runner._run_test({
                "name": "sample failure",
                "command": "sample.command",
                "expect": {"status": "ok"},
                "asserts": [{"path": "data.value", "op": "==", "value": 1}],
            })
        self.assertFalse(passed)
        case = runner.case_results[-1]
        self.assertEqual(case["id"], "sample.json::case-0001")
        self.assertEqual(case["assertions"][0]["value"], 1)
        self.assertEqual(case["failure_reason"], "Status: error != ok")

    def test_blocked_run_does_not_rotate_completed_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / "regression_report.json"
            previous = Path(tmp) / "regression_previous.json"
            failure_inventory = Path(tmp) / "regression_failures.json"
            temporary = Path(tmp) / "regression_report.json.tmp"
            prior = {"status": "completed", "summary": {"passed": 3}, "suites": []}
            report.write_text(json.dumps(prior), encoding="utf-8")
            previous.write_text('{"status":"older"}', encoding="utf-8")
            failure_inventory.write_text('{"failed_cases":[]}', encoding="utf-8")
            blocked = {"status": "blocked", "blocked": [{"reason": "offline"}]}
            run_suites_all._atomic_write_json(temporary, blocked)

            self.assertEqual(json.loads(report.read_text()), prior)
            self.assertEqual(previous.read_text(), '{"status":"older"}')
            self.assertEqual(failure_inventory.read_text(), '{"failed_cases":[]}')

    def test_completed_run_rotates_and_writes_failure_inventory(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / "regression_report.json"
            previous = Path(tmp) / "regression_previous.json"
            failures = Path(tmp) / "regression_failures.json"
            temporary = Path(tmp) / "regression_report.json.tmp"
            prior = {"status": "completed", "summary": {"passed": 2}, "suites": []}
            report.write_text(json.dumps(prior), encoding="utf-8")
            current = {
                "status": "completed",
                "finished_at": "now",
                "summary": {"failed": 1},
                "suites": [{"suite": "/x/suite.json", "cases": [{
                    "id": "suite.json::case-0001", "name": "bad", "command": "x",
                    "status": "failed", "expected": {"status": "ok"},
                    "assertions": [], "actual": {"status": "error"},
                    "failure_reason": "status mismatch",
                }]}],
            }
            run_suites_all._atomic_write_json(temporary, current)
            run_suites_all._publish_completed_report(report, temporary, current)

            self.assertEqual(json.loads(previous.read_text()), prior)
            self.assertEqual(json.loads(report.read_text()), current)
            inventory = json.loads(failures.read_text())
            self.assertEqual(inventory["failed_cases"][0]["id"], "suite.json::case-0001")
            self.assertEqual(
                inventory["failed_cases"][0]["evidence"]["failure_reason"],
                "status mismatch",
            )
            self.assertEqual(inventory["classification_counts"]["UNCLASSIFIED"], 1)
            self.assertEqual(sum(inventory["classification_counts"].values()), 1)

    def test_json_runner_accepts_explicit_endpoint(self):
        with patch.object(json_runner, "JsonSuiteRunner") as runner_type:
            runner_type.return_value.run_suite.return_value = True
            result = json_runner.main(
                ["suite.json", "--host", "127.0.0.1", "--port", "12483"]
            )
        self.assertEqual(result, 0)
        runner_type.assert_called_once_with("127.0.0.1", 12483, timeout=25,
                                            capture_responses=None)
        runner_type.return_value.run_suite.assert_called_once_with("suite.json")

    def test_json_runner_supports_environment_endpoint(self):
        with patch.dict(os.environ, {"TWCLONE_TEST_HOST": "qa.local", "TWCLONE_TEST_PORT": "12484"}):
            with patch.object(json_runner, "JsonSuiteRunner") as runner_type:
                runner_type.return_value.run_suite.return_value = True
                json_runner.main(["suite.json"])
        runner_type.assert_called_once_with("qa.local", 12484, timeout=25,
                                            capture_responses=None)

    def test_fixture_guard_accepts_loopback_qa_database(self):
        rig_db.validate_qa_target("twclone_v3qa_ci_123", "127.0.0.1", 55438)

    def test_fixture_guard_rejects_shared_or_remote_targets(self):
        with self.assertRaises(ValueError):
            rig_db.validate_qa_target("twclone", "127.0.0.1")
        with self.assertRaises(ValueError):
            rig_db.validate_qa_target("twclone_qa_ci_123", "192.168.25.5")
        with self.assertRaisesRegex(ValueError, "port 5432"):
            rig_db.validate_qa_target("twclone_v3qa_ci_123", "127.0.0.1", 5432)

    def test_regression_connections_are_scoped_to_loopback_qa(self):
        admin, app = run_regression_instance.build_qa_conninfos(
            "twclone_v3qa_ci_123", "127.0.0.1", 55438, "qa_user", "qa-secret"
        )
        self.assertIn("dbname=postgres", admin)
        self.assertIn("host=127.0.0.1", admin)
        self.assertIn("dbname=%DB%", app)
        self.assertIn("host=127.0.0.1", app)
        self.assertNotIn("192.168.25.5", admin + app)

    def test_regression_rejects_remote_admin_cleanup_connection(self):
        with self.assertRaises(ValueError):
            run_regression_instance.build_qa_conninfos(
                "twclone_v3qa_ci_123", "127.0.0.1", 55438, "qa_user",
                admin_conn="dbname=postgres host=192.168.25.5 port=55438",
            )

    def test_regression_refuses_live_default_postgres_port(self):
        with self.assertRaisesRegex(ValueError, "port 5432"):
            run_regression_instance.build_qa_conninfos(
                "twclone_v3qa_ci_123", "127.0.0.1", 5432, "qa_user", "qa-secret"
            )

    def test_response_capture_redacts_session_tokens(self):
        response = {"data": {"session_token": "secret", "player_id": 7}}
        self.assertEqual(
            json_runner.JsonSuiteRunner._redact_response(response),
            {"data": {"session_token": "<redacted>", "player_id": 7}},
        )


if __name__ == "__main__":
    unittest.main()
