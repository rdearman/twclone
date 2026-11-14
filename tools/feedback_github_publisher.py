#!/usr/bin/env python3
"""PostgreSQL feedback outbox worker. Queue-only unless GitHub App settings exist."""
from __future__ import annotations

import json
import os
import time
import email.utils
import urllib.error
import urllib.parse
import urllib.request

try:
    import psycopg
except ImportError:  # pragma: no cover - deployment reports this as unavailable
    psycopg = None

REPOSITORY = os.getenv("TWCLONE_FEEDBACK_REPOSITORY", "rdearman/twclone")
API = os.getenv("TWCLONE_GITHUB_API", "https://api.github.com")
LABELS = ("player-bug", "player-suggestion", "client-python", "client-godot", "v3-playtest", "needs-triage")


class GitHubAPIError(RuntimeError):
    def __init__(self, status, retry_after=None):
        super().__init__(f"GitHub HTTP {status}")
        self.status = status
        self.retry_after = retry_after


def retry_delay(attempts, retry_after=None):
    delay = min(3600, 10 * (2 ** min(int(attempts), 8)))
    if retry_after is not None:
        try:
            delay = max(delay, int(float(retry_after)))
        except (TypeError, ValueError):
            try:
                delay = max(delay, int(email.utils.parsedate_to_datetime(str(retry_after)).timestamp() - time.time()))
            except (TypeError, ValueError, OverflowError):
                pass
    return max(1, min(3600, delay))


def github_enabled(env=os.environ):
    required = ("TWCLONE_FEEDBACK_GITHUB_ENABLED", "TWCLONE_GITHUB_APP_ID",
                "TWCLONE_GITHUB_INSTALLATION_ID", "TWCLONE_GITHUB_PRIVATE_KEY")
    return env.get(required[0], "false").lower() == "true" and all(env.get(k) for k in required[1:])


def issue_content(row):
    report_id, player_id, kind, subject, description, expected, actual, steps, diagnostics, client, version, build, protocol = row
    label = "Bug" if kind == "bug" else "Suggestion"
    safe_subject = " ".join(str(subject).split())[:100]
    title = f"[V3][{client.title()}][{label}] {safe_subject}"
    body = [f"## Player description\n{description}"]
    if kind == "bug":
        body += [f"## Expected behaviour\n{expected}", f"## Actual behaviour\n{actual}"]
    if steps:
        body.append(f"## Reproduction steps\n{steps}")
    body += [f"## Versions\n- Client: {client} {version}\n- Build: {build}\n- Protocol: {protocol}",
             "## Sanitised diagnostics\n```json\n" + json.dumps(diagnostics, ensure_ascii=True, indent=2) + "\n```",
             f"<!-- twclone-report-id:{report_id} -->"]
    return title, "\n\n".join(body)


class GitHubApp:
    def __init__(self, env=os.environ):
        self.env = env
        self.api = env.get("TWCLONE_GITHUB_API", "https://api.github.com")
        self.token = None
        self.expires = 0

    def _request(self, method, path, payload=None, token=None):
        data = None if payload is None else json.dumps(payload).encode()
        req = urllib.request.Request(self.api + path, data=data, method=method,
            headers={"Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28",
                     "Content-Type": "application/json", "Authorization": "Bearer " + (token or self._installation_token())})
        try:
            with urllib.request.urlopen(req, timeout=20) as response:
                return response.status, json.loads(response.read() or b"{}"), response.headers
        except urllib.error.HTTPError as exc:
            # Never retain upstream response text: it is neither needed for
            # retry policy nor safe to copy into player-visible state.
            retry_after = exc.headers.get("Retry-After")
            reset = exc.headers.get("X-RateLimit-Reset")
            if not retry_after and reset and exc.headers.get("X-RateLimit-Remaining") == "0":
                retry_after = str(max(1, int(reset) - int(time.time())))
            raise GitHubAPIError(exc.code, retry_after) from None

    def _installation_token(self):
        if self.token and self.expires > time.time() + 60:
            return self.token
        try:
            import jwt
        except ImportError as exc:
            raise RuntimeError("PyJWT with cryptography support is required for GitHub App publication") from exc
        with open(self.env["TWCLONE_GITHUB_PRIVATE_KEY"], encoding="utf-8") as f:
            key = f.read()
        now = int(time.time())
        app_jwt = jwt.encode({"iat": now - 30, "exp": now + 540, "iss": self.env["TWCLONE_GITHUB_APP_ID"]}, key, algorithm="RS256")
        path = f"/app/installations/{urllib.parse.quote(self.env['TWCLONE_GITHUB_INSTALLATION_ID'])}/access_tokens"
        code, result, _ = self._request("POST", path, {}, token=app_jwt)
        if code != 201 or not result.get("token"):
            raise RuntimeError("GitHub App installation token request failed")
        self.token = result["token"]
        self.expires = time.time() + 3000
        return self.token

    def reconcile(self, report_id):
        query = urllib.parse.urlencode({"q": f'repo:{REPOSITORY} in:body "twclone-report-id:{report_id}"'})
        _, result, _ = self._request("GET", "/search/issues?" + query)
        items = result.get("items", [])
        if items:
            item = items[0]
            return {"number": item["number"], "html_url": item["html_url"]}
        return None

    def publish(self, title, body, kind, client):
        owner, repo = REPOSITORY.split("/", 1)
        base = f"/repos/{urllib.parse.quote(owner)}/{urllib.parse.quote(repo)}"
        for label in LABELS:
            try:
                self._request("POST", base + "/labels", {"name": label, "color": "1d76db"})
            except RuntimeError as exc:
                if "HTTP 422" not in str(exc) and "HTTP 403" not in str(exc):
                    raise
        desired = ["player-bug" if kind == "bug" else "player-suggestion",
                   "client-" + client, "v3-playtest", "needs-triage"]
        try:
            _, known_labels, _ = self._request("GET", base + "/labels?per_page=100")
            known = {item.get("name") for item in known_labels if isinstance(item, dict)}
            labels = [label for label in desired if label in known]
        except Exception:
            # Labels are helpful metadata, but an inaccessible label endpoint
            # must not prevent the player report itself from being delivered.
            labels = []
        _, result, _ = self._request("POST", base + "/issues", {"title": title, "body": body, "labels": labels})
        return {"number": result["number"], "html_url": result["html_url"]}


def publish_one(conn, gh):
    with conn.transaction():
        row = conn.execute("""
          SELECT report_id::text, player_id, report_type, subject, description,
                 expected_behavior, actual_behavior, reproduction_steps, diagnostics,
                 client_name, client_version, build_id, protocol_version,
                 public_issue_title, public_issue_body
            FROM player_feedback_reports
           WHERE ((publication_state IN ('pending','retryable') AND next_retry_at <= NOW())
              OR (publication_state='processing' AND updated_at < NOW() - INTERVAL '10 minutes'))
           ORDER BY created_at
           FOR UPDATE SKIP LOCKED LIMIT 1
        """).fetchone()
        if not row:
            return False
        report_id = row[0]
        conn.execute("UPDATE player_feedback_reports SET publication_state='processing',attempt_count=attempt_count+1,updated_at=NOW() WHERE report_id=%s", (report_id,))
    try:
        existing = gh.reconcile(report_id)
        if not existing:
            title, body = (row[13], row[14]) if row[13] and row[14] else issue_content(row)
            existing = gh.publish(title, body, row[2], row[9])
        with conn.transaction():
            conn.execute("UPDATE player_feedback_reports SET publication_state='published',github_issue_number=%s,github_issue_url=%s,last_publication_error='',updated_at=NOW() WHERE report_id=%s",
                         (existing["number"], existing["html_url"], report_id))
    except Exception as exc:
        # No request/response body, token or exception detail is persisted.
        attempts = conn.execute("SELECT attempt_count FROM player_feedback_reports WHERE report_id=%s", (report_id,)).fetchone()[0]
        delay = retry_delay(attempts, getattr(exc, "retry_after", None))
        with conn.transaction():
            conn.execute("UPDATE player_feedback_reports SET publication_state=%s,last_publication_error=%s,next_retry_at=NOW()+(%s * INTERVAL '1 second'),updated_at=NOW() WHERE report_id=%s",
                         ("failed" if attempts >= 10 else "retryable", "GitHub publication failed; retry scheduled.", delay, report_id))
    return True


def run():
    if not github_enabled():
        print("feedback publisher: queue-only mode (GitHub publication disabled)", flush=True)
        return
    if psycopg is None:
        raise RuntimeError("psycopg is required for the feedback publisher")
    conninfo = os.environ.get("TWCLONE_DB_APP_CONNINFO")
    if not conninfo:
        raise RuntimeError("TWCLONE_DB_APP_CONNINFO is required")
    gh = GitHubApp()
    with psycopg.connect(conninfo, autocommit=True) as conn:
        while True:
            if not publish_one(conn, gh):
                time.sleep(2)


if __name__ == "__main__":
    run()
