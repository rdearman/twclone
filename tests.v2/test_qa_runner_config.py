"""Regression checks for configurable, isolated V3 integration runners."""

import os
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests.v2"))

import json_runner
import rig_db


class RunnerConfigurationTests(unittest.TestCase):
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
        rig_db.validate_qa_target("twclone_v3qa_ci_123", "127.0.0.1")

    def test_fixture_guard_rejects_shared_or_remote_targets(self):
        with self.assertRaises(ValueError):
            rig_db.validate_qa_target("twclone", "127.0.0.1")
        with self.assertRaises(ValueError):
            rig_db.validate_qa_target("twclone_qa_ci_123", "192.168.25.5")

    def test_response_capture_redacts_session_tokens(self):
        response = {"data": {"session_token": "secret", "player_id": 7}}
        self.assertEqual(
            json_runner.JsonSuiteRunner._redact_response(response),
            {"data": {"session_token": "<redacted>", "player_id": 7}},
        )


if __name__ == "__main__":
    unittest.main()
