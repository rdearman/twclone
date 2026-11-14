"""Manual disposable-server acceptance through real Python client workflows.

Run only against a fresh disposable V3 server:
  TWCLONE_FEEDBACK_ACCEPTANCE_PORT=55501 python3 tests.v2/feedback_live_acceptance.py

The script registers ordinary players and does not use SysOp/database writes.
Its output is redacted before printing.
"""
from __future__ import annotations

import builtins
import contextlib
import io
import os
import re
import socket
import sys
import uuid

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "client", "python_client"))
from client import feedback_report_flow, register_account  # noqa: E402
from protocol import Conn  # noqa: E402
from state import Context  # noqa: E402


def _ordinary_account(host: str, port: int):
    name = "feedback_" + uuid.uuid4().hex[:10]
    conn = Conn(socket.create_connection((host, port), timeout=5))
    result = register_account(conn, name, "Acceptance-Only-Pass-92", "QA Ship")
    if result.get("status") != "ok":
        raise AssertionError("public ordinary-player registration failed")
    ctx = Context(conn, {})
    conn.ctx = ctx
    return name, conn, ctx


def main() -> int:
    host = os.getenv("TWCLONE_FEEDBACK_ACCEPTANCE_HOST", "127.0.0.1")
    port = int(os.getenv("TWCLONE_FEEDBACK_ACCEPTANCE_PORT", "55501"))
    _, conn, ctx = _ordinary_account(host, port)
    answers = iter([
        "b", "Feedback sanitizer acceptance",
        "Contact accept.person@example.org password=NeverPwd123 token=NeverToken123 at /home/qa/private.txt",
        "Sensitive values must be removed", "The server should redact planted values",
        "Open the report menu", "y", "25", "YES",
    ])
    old_input = builtins.input
    transcript = io.StringIO()
    try:
        builtins.input = lambda _prompt="": next(answers)
        with contextlib.redirect_stdout(transcript):
            feedback_report_flow(ctx)
    finally:
        builtins.input = old_input
    output = transcript.getvalue()
    for planted in ("accept.person@example.org", "NeverPwd123", "NeverToken123", "/home/qa/private.txt"):
        if planted in output or planted in str(conn.diagnostics.recent(100)):
            raise AssertionError("planted sensitive value escaped client redaction")
    match = re.search(r"Report reference: ([0-9a-f-]{36})", output)
    if not match:
        raise AssertionError("client menu flow did not submit a report")
    report_id = match.group(1)
    status = conn.rpc("feedback.status", {"report_id": report_id})
    diagnostics = conn.rpc("feedback.diagnostics", {"report_id": report_id})
    if status.get("status") != "ok" or diagnostics.get("status") != "ok":
        raise AssertionError("owner status/diagnostics requests failed")
    _, other, _ = _ordinary_account(host, port)
    if other.rpc("feedback.status", {"report_id": report_id}).get("status") == "ok":
        raise AssertionError("cross-player report lookup was allowed")
    print("PASS registration, preview, explicit submit, status, diagnostics and owner isolation")
    print("report_id=" + report_id)
    print("sanitised_preview=" + re.sub(r"\s+", " ", output).strip()[:1000])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
