import json
import urllib.parse
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import feedback_github_publisher as publisher


class MockGitHub(BaseHTTPRequestHandler):
    issues = []
    labels = set()
    fail_issues = False
    deny_label_creation = False
    rate_limit_issue = False

    def do_GET(self):
        if self.path.startswith("/search/issues"):
            query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query).get("q", [""])[0]
            marker = query.split("twclone-report-id:", 1)[-1].strip('"') if "twclone-report-id:" in query else ""
            data = {"items": [item for item in self.issues if marker in item.get("body", "")]}
            self._reply(200, data)
        elif "/labels" in self.path:
            self._reply(200, [{"name": label} for label in sorted(self.labels)])
        else:
            self._reply(404, {})

    def do_POST(self):
        payload = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))))
        if self.path.endswith("/labels"):
            if self.deny_label_creation:
                self._reply(403, {"message": "label creation forbidden"})
                return
            if payload["name"] in self.labels:
                self._reply(422, {"message": "already exists"})
            else:
                self.labels.add(payload["name"])
                self._reply(201, payload)
        elif self.path.endswith("/issues"):
            if self.rate_limit_issue:
                self.send_response(403)
                self.send_header("Retry-After", "45")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            if self.fail_issues:
                self._reply(500, {"message": "upstream secret NeverPersistThis"})
                return
            item = {"number": len(self.issues) + 1, "html_url": "https://example.test/issues/1",
                    "title": payload["title"], "body": payload["body"], "labels": payload.get("labels", [])}
            self.issues.append(item)
            self._reply(201, item)
        else:
            self._reply(404, {})

    def _reply(self, code, value):
        raw = json.dumps(value).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def log_message(self, *_args):
        pass


def test_issue_body_has_stable_report_marker_and_required_labels():
    row = ("00000000-0000-4000-8000-000000000001", 12, "bug", "Warp broke", "Description",
           "Expected movement", "Stayed put", "1. Warp", [{"command": "move.warp"}],
           "godot", "4.5.2", "build-x", "v3")
    title, body = publisher.issue_content(row)
    assert title == "[V3][Godot][Bug] Warp broke"
    assert "twclone-report-id:00000000-0000-4000-8000-000000000001" in body
    assert "Sanitised diagnostics" in body
    assert "Stayed put" in body


def test_local_mock_github_publication_and_duplicate_reconciliation():
    MockGitHub.issues = []
    MockGitHub.labels = set()
    MockGitHub.fail_issues = False
    MockGitHub.deny_label_creation = False
    server = HTTPServer(("127.0.0.1", 0), MockGitHub)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        gh = publisher.GitHubApp({"TWCLONE_GITHUB_API": f"http://127.0.0.1:{server.server_port}",
                                  "TWCLONE_GITHUB_APP_ID": "mock", "TWCLONE_GITHUB_INSTALLATION_ID": "1"})
        gh.token, gh.expires = "mock-token", 9999999999
        row = ("00000000-0000-4000-8000-000000000002", 13, "suggestion", "Add map", "Map idea",
               "", "", "", [], "python", "3", "build-y", "v3")
        found = gh.reconcile(row[0])
        assert found is None
        title, body = publisher.issue_content(row)
        result = gh.publish(title, body, row[2], row[9])
        assert result["number"] == 1
        assert len(MockGitHub.issues) == 1
        # A timeout retry reconciles by the stable body marker and does not call create again.
        MockGitHub.issues[0]["body"] = body
        assert gh.reconcile(row[0])["number"] == 1
        assert len(MockGitHub.issues) == 1
        assert set(publisher.LABELS).issubset(MockGitHub.labels)
    finally:
        server.shutdown()
        server.server_close()


class FakeTransaction:
    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False


class FakeCursor:
    def __init__(self, row):
        self.row = row

    def fetchone(self):
        return self.row


class FakeConnection:
    def __init__(self, row):
        self.row = row
        self.state = "pending"
        self.attempts = 0
        self.saved_error = ""
        self.issue = None
        self.retry_seconds = 0

    def transaction(self):
        return FakeTransaction()

    def execute(self, sql, params=()):
        if "SELECT report_id::text" in sql:
            if self.state == "pending":
                return FakeCursor(self.row)
            return FakeCursor(None)
        if "SELECT attempt_count" in sql:
            return FakeCursor((self.attempts,))
        if "SET publication_state='processing'" in sql:
            self.state = "processing"
            self.attempts += 1
        elif "SET publication_state='published'" in sql:
            self.state = "published"
            self.issue = params[0]
        elif "SET publication_state=%s" in sql:
            self.state, self.saved_error = params[0], params[1]
            self.retry_seconds = int(params[2])
        return FakeCursor(None)


def test_outbox_worker_persists_success_and_sanitized_retry_failure():
    MockGitHub.issues = []
    MockGitHub.labels = set()
    MockGitHub.fail_issues = False
    MockGitHub.deny_label_creation = False
    MockGitHub.rate_limit_issue = False
    server = HTTPServer(("127.0.0.1", 0), MockGitHub)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        gh = publisher.GitHubApp({"TWCLONE_GITHUB_API": f"http://127.0.0.1:{server.server_port}"})
        gh.token, gh.expires = "mock-token", 9999999999
        row = ("00000000-0000-4000-8000-000000000003", 14, "bug", "Issue", "Description",
               "Expected", "Actual", "", {"command": "sector.info"}, "python", "3", "", "v3",
               "[V3][Python][Bug] Issue", "## exact preview\n<!-- twclone-report-id:00000000-0000-4000-8000-000000000003 -->")
        conn = FakeConnection(row)
        assert publisher.publish_one(conn, gh)
        assert conn.state == "published" and conn.issue == 1
        assert MockGitHub.issues[0]["body"] == row[14]

        MockGitHub.fail_issues = True
        failure_id = "00000000-0000-4000-8000-000000000004"
        failure_row = (failure_id,) + row[1:13] + ("[V3][Python][Bug] Failure", f"failure body\n<!-- twclone-report-id:{failure_id} -->")
        retry = FakeConnection(failure_row)
        assert publisher.publish_one(retry, gh)
        assert retry.state == "retryable"
        assert "NeverPersistThis" not in retry.saved_error
        assert retry.saved_error == "GitHub publication failed; retry scheduled."

        MockGitHub.fail_issues = False
        MockGitHub.rate_limit_issue = True
        limited_id = "00000000-0000-4000-8000-000000000005"
        limited_row = (limited_id,) + row[1:13] + ("[V3][Python][Bug] Limited", f"limited\n<!-- twclone-report-id:{limited_id} -->")
        limited = FakeConnection(limited_row)
        assert publisher.publish_one(limited, gh)
        assert limited.state == "retryable" and limited.retry_seconds >= 45
    finally:
        MockGitHub.fail_issues = False
        MockGitHub.rate_limit_issue = False
        server.shutdown()
        server.server_close()


def test_publication_continues_without_permission_to_create_labels():
    MockGitHub.issues = []
    MockGitHub.labels = set()
    MockGitHub.fail_issues = False
    MockGitHub.deny_label_creation = True
    MockGitHub.rate_limit_issue = False
    server = HTTPServer(("127.0.0.1", 0), MockGitHub)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        gh = publisher.GitHubApp({"TWCLONE_GITHUB_API": f"http://127.0.0.1:{server.server_port}"})
        gh.token, gh.expires = "mock-token", 9999999999
        result = gh.publish("[V3][Python][Suggestion] Idea", "Body", "suggestion", "python")
        assert result["number"] == 1
        issue = MockGitHub.issues[0]
        assert issue["title"].endswith("Idea")
        assert issue["labels"] == []
    finally:
        MockGitHub.deny_label_creation = False
        server.shutdown()
        server.server_close()


def test_publisher_defaults_to_queue_only_without_app_configuration():
    assert publisher.github_enabled({}) is False
    assert publisher.github_enabled({
        "TWCLONE_FEEDBACK_GITHUB_ENABLED": "false",
        "TWCLONE_GITHUB_APP_ID": "app",
        "TWCLONE_GITHUB_INSTALLATION_ID": "installation",
        "TWCLONE_GITHUB_PRIVATE_KEY": "/secret/key.pem",
    }) is False
