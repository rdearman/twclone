# V3 player feedback acceptance — 14 November 2026

## Scope and environment

This run prioritised the complete feedback path. It used the task-created
PostgreSQL 16 container `twclone-feedback-db-20261114` and V3 server
`twclone-feedback-server-20261114` on loopback port 55501. The database was
freshly bootstrapped from the current source. No beta/production service or
host PostgreSQL was used. Public GitHub publication remained disabled; the
publisher ran in queue-only mode. No public issue was created.

The serving server was restarted after an earlier report submission. The
disposable Docker entrypoint now copies `bigbang.json` into a private writable
`mktemp` runtime directory instead of writing the read-only image path. The
restarted server accepted a new Python login and status request.

## Executed player journey

`python3 /tmp/twclone-feedback-acceptance.py` drove the actual Python client
registration helper and `feedback_report_flow` with ordinary public accounts.
The handler called the real V3 `feedback.preview` and `feedback.submit` RPCs.
The acceptance input planted an email, password, token, and home-directory
path in player-authored text.

| Operation | Result | Evidence |
|---|---|---|
| Register ordinary player | PASS | Public `auth.register` returned a session; player ID 106 was observed in PostgreSQL for the first run. |
| Open Python feedback menu workflow | PASS | Actual `feedback_report_flow` ran; a test input provider supplied form responses and typed exact `YES` confirmation. |
| Preview bug report | PASS | Server response contained `[V3][Python][Bug]` title and sanitised body. Email, credential assignments, and path were redacted before display. |
| Explicit submit | PASS | `feedback.submit` returned stable UUID and `pending`. |
| Exact preview persistence | PASS | Acceptance compared the displayed preview bytes with `public_issue_title || blank line || public_issue_body` read from PostgreSQL; exact match. |
| Diagnostics retrieval | PASS | Owner's `feedback.diagnostics` returned sanitised stored diagnostics only. |
| Status ownership | PASS | Owner status read succeeded; a second registered ordinary player received a non-success response for the same report UUID. |
| Secret persistence/log scan | PASS | Read-only SQL found no planted email/password/token/path in report text or diagnostic JSON; targeted disposable server-log scan returned no matches. |
| Version/build/nested diagnostic redaction | PASS | A live server probe planted an email, IPv6 address, credential, and home path in client metadata and nested diagnostics. Preview plus five PostgreSQL assertions confirmed all were removed. Report `a46f801d-9eb5-4214-8381-425a2c50e5ad`. |
| Serving server restart | PASS | Restarted V3 server listened again on loopback; owner logged in through the Python client and queried the prior report as `pending`. |
| Queue-only behavior | PASS | Server log: `feedback publisher: queue-only mode (GitHub publication disabled)`; report remained locally queued. |

Most recent byte-for-byte preview report UUID:
`e9de83eb-f674-491a-af32-f402e8705ce6`. The report whose status was re-read
after serving-server restart was
`b46a3539-af3f-49aa-ab71-3c5d264bd3b3`. A synchronized duplicate retry used a
separate account and persisted one row; an injected rollback account persisted
zero rows. Full sanitised client transcripts are retained in
`/tmp/twclone-feedback-transcript.txt` for this disposable run; it is not
committed because it is test output.

## Automated test results

- Full Python client plus feedback mock suite:
  **812 passed** (`python3 -m pytest -q client/python_client/tests tests.v2/test_feedback_publisher.py`).
- Full `tests.v2`: **259 passed, 7 skipped, 227 subtests passed**
  (`python3 -m pytest -q tests.v2`).
- Feedback diagnostics and local GitHub publisher focused suite: **10 passed**. Run with loopback permission:
  `python3 -m pytest -q client/python_client/tests/test_diagnostics.py tests.v2/test_feedback_publisher.py`.
- Python diagnostics tests alone after IPv6 redaction: **5 passed**.
- PostgreSQL/MySQL BigBang selection and manifest checks are included in the
  passing full `tests.v2` suite.
- Current C server source compiles with `make -C bin -j2 server` (ASan/UBSan
  build).
- `python3 -m py_compile` passed for the publisher and Python client modules.

The loopback mock tests fail under the default filesystem/network sandbox with
`PermissionError: Operation not permitted` while binding the test HTTP server;
they passed when run with loopback enabled. This is an execution permission
limit, not a test failure.

## Implementation status

- **IMPLEMENTED:** authenticated `feedback.preview`, `feedback.submit`,
  `feedback.status`, and `feedback.diagnostics`; PostgreSQL outbox schema;
  per-player/global limits; ownership checks; idempotency; exact preview
  persistence; Python and Godot reporting/status UI; bounded client diagnostic
  buffers; server/client allowlist sanitation; queue-only outbox publisher;
  GitHub App token, issue creation, label mapping, retry, and UUID reconciliation.
- **VERIFIED AGAINST LIVE DISPOSABLE SERVER:** ordinary Python registration,
  feedback preview/explicit submit, status and diagnostic reads, other-player
  denial, exact preview persistence, planted-secret redaction, and server
  restart persistence.
- **VERIFIED AGAINST LIVE DISPOSABLE POSTGRESQL:** two authenticated sessions
  submitted the same UUID concurrently; one response inserted and one replayed
  idempotently, with exactly one persisted row. An injected `BEFORE INSERT`
  failure returned a generic error and left zero report rows.
- **SECURITY DEFECT FOUND AND FIXED:** an IP regex match later in a diagnostic
  string could previously copy preceding text directly to output, skipping an
  earlier email. The server now replaces addresses only at the current scan
  cursor and preserves the matched boundary character. The planted
  email-before-IPv6 live probe passes.
- **TESTED WITH MOCKS:** GitHub issue creation, stable report marker
  reconciliation, worker success-state update, and sanitised retryable failure
  handling, `Retry-After` rate-limit delay, queue-only defaults, and issue
  creation when label permissions are unavailable. Ten focused feedback tests passed.
- **NOT VERIFIED:** Godot runtime/interface execution. No Godot executable is
  installed; the available `godot3` package candidate is not compatible with
  this Godot 4.5 project. `gdlint` also could not open files in this managed
  read-only lint execution context, so no lint result is claimed.
- **NOT VERIFIED:** real GitHub App authentication/publication. No credentials
  were requested or supplied, and tests did not contact GitHub.
- **NOT VERIFIED:** exact server per-player/global submission-limit boundary
  and global-limit contention. GitHub API `Retry-After` behavior is mock-tested.
- **NOT RUN:** full Godot suite (196 command-menu cases and the
  `planet_command_response_test.gd` regression), actual Godot gameplay journey,
  and full StarPort concurrency/rollback suite in this feedback-focused
  checkpoint.

No change is made to external beta readiness. Python feedback is useful and
live-tested, but Godot feedback and the outstanding gameplay acceptance suite
remain unverified.
