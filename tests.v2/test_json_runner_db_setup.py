import unittest

from json_runner import JsonSuiteRunner


class FakeClient:
    def __init__(self):
        self.session_token = "qa-session"
        self.sent = []
        self.responses = [
            {"status": "ok", "type": "sys.raw_sql_exec", "data": {"rows": []}},
            {"status": "ok", "type": "sys.raw_sql_exec", "data": {"rows": [[42]]}},
        ]

    def is_connected(self, max_idle_seconds=8.0):
        return True

    def send_json(self, request):
        self.sent.append(request)

    def recv_next_non_notice(self):
        return self.responses.pop(0)

    def close(self):
        self.closed = True


class AmbiguousDisconnectClient(FakeClient):
    def __init__(self):
        super().__init__()
        self.closed = False

    def recv_next_non_notice(self):
        raise ConnectionError("ambiguous disconnect after request send")


class JsonRunnerFixtureTest(unittest.TestCase):
    def test_db_setup_does_not_skip_case_command_or_save(self):
        runner = JsonSuiteRunner.__new__(JsonSuiteRunner)
        runner._admin_client = FakeClient()
        runner.vars = {}
        runner.user_clients = {}
        runner.user_credentials = {}
        runner.capture_responses = None
        runner.client_idle_timeout_seconds = 8.0
        runner.active_suite = "fixture-test.json"
        runner._current_case_id = "fixture-test.json::case-0001"
        runner._current_response = None
        runner._current_failure_reason = None

        passed = runner._run_test_impl({
            "name": "fixture followed by command",
            "db_setup": {"sql": ["SELECT 1"]},
            "command": "sys.raw_sql_exec",
            "user": "admin",
            "data": {"sql": "SELECT 42"},
            "expect": {"status": "ok"},
            "save": {"captured_id": "data.rows.0.0"},
        })

        self.assertTrue(passed)
        self.assertEqual(len(runner._admin_client.sent), 2)
        self.assertEqual(runner._admin_client.sent[0]["data"]["sql"], "SELECT 1")
        self.assertEqual(runner._admin_client.sent[1]["command"], "sys.raw_sql_exec")
        self.assertEqual(runner.vars["captured_id"], 42)

    def test_non_idempotent_request_is_not_replayed_after_ambiguous_disconnect(self):
        runner = JsonSuiteRunner.__new__(JsonSuiteRunner)
        client = AmbiguousDisconnectClient()
        runner.user_clients = {"market-player": client}
        runner.user_credentials = {}
        runner.client_idle_timeout_seconds = 8.0
        runner._admin_client = None
        runner.capture_responses = None
        runner.vars = {}
        runner.active_suite = "disconnect-test.json"
        runner.case_results = []
        runner._suite_case_index = 0

        passed = runner._run_test({
            "name": "Do not replay a potentially settled transfer",
            "command": "bank.transfer",
            "user": "market-player",
            "data": {"to_player_id": 42, "amount": 100},
            "expect": {"status": "ok"},
        })

        self.assertFalse(passed)
        self.assertEqual(len(client.sent), 1)
        self.assertEqual(client.sent[0]["command"], "bank.transfer")
        self.assertTrue(client.closed)
        self.assertNotIn("market-player", runner.user_clients)
        self.assertEqual(runner.case_results[0]["status"], "failed")
        self.assertIn("ambiguous disconnect", runner.case_results[0]["failure_reason"])


if __name__ == "__main__":
    unittest.main()
