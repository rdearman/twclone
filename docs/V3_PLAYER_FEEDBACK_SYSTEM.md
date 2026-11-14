# V3 player feedback system

## Architecture and player experience

Both clients expose **Report Bug / Suggest Improvement**, **Report This Error**,
and a report-status lookup. A report collects subject, description, bug
expected/actual behaviour, optional reproduction steps, a diagnostics toggle,
and one of 10, 25, 50, or 100 recent exchanges (25 by default). The client
requests `feedback.preview`, displays the server's sanitised public title and
body, and submits only after explicit confirmation. A report reference and
publication state are displayed; a GitHub URL appears when available.

The Python `Conn` and Godot `ProtocolTransport` keep bounded in-memory buffers
with a maximum of 100 exchanges. Authentication and private communications
payloads are omitted. Captured fields are allowlisted and diagnostic text is
sanitised before buffering. Recent snapshots are copied so later mutations do
not rewrite the buffer.

## Protocol and persistence

`feedback.preview`, `feedback.submit`, `feedback.status`, and
`feedback.diagnostics` are authenticated V3 commands. See
[`docs/PROTOCOL.v3/31_Player_Feedback.md`](PROTOCOL.v3/31_Player_Feedback.md)
for request and response fields.

Migration `sql/pg/188_v3_player_feedback_outbox.sql` creates the report/outbox
table, player/idempotency uniqueness, publication-state constraints, and
indexes. The table stores the exact server-generated preview title/body so a
publisher cannot reconstruct a different public document after JSONB key
ordering changes. Submission persists report and publication state in one
transaction. A UUID key makes retries idempotent for each player. The MySQL
script keeps the portable schema inventory aligned; the current V3 server and
publisher runtime use PostgreSQL.

The per-player default is five new reports per hour and the global default is
500 per hour. Operators can set `TWCLONE_FEEDBACK_PLAYER_HOURLY` and
`TWCLONE_FEEDBACK_GLOBAL_HOURLY` to positive integer limits. Payload limits are
enforced in the C handler. Preview is read-only; no feedback command modifies
gameplay state.

## Redaction and privacy

Diagnostics use a closed field allowlist and omit unknown fields. Passwords,
tokens, secrets, cookies, authorization material, email/IP fields, paths,
private chat/mail, and message bodies are excluded. Authentication and private
communication exchanges retain only command, outcome, request ID, and latency
metadata. Text redaction also masks common email/IP/path/credential/JWT/GitHub
token forms. The server repeats filtering and text sanitisation before writing
to PostgreSQL. Server errors returned to clients contain generic feedback
messages rather than SQL, credentials, or exception text.

The player-facing confirmation is generated server-side. The exact title/body
are persisted with the report and used by the publisher. User-authored content
is data only; the server does not interpret or execute it.

The server text sanitizer processes input left-to-right. IP matching consumes
only a match at the current scan cursor, so an address later in a diagnostic
string cannot cause preceding email or credential text to bypass sanitisation.
The live acceptance probe places an email before an IPv6 address to guard this.

## GitHub App publication

`tools/feedback_github_publisher.py` is an asynchronous PostgreSQL outbox
worker. GitHub publication is disabled unless
`TWCLONE_FEEDBACK_GITHUB_ENABLED=true`, `TWCLONE_GITHUB_APP_ID`,
`TWCLONE_GITHUB_INSTALLATION_ID`, and `TWCLONE_GITHUB_PRIVATE_KEY` are
configured. The private-key value is a server-side file path; mount the
credential through the deployment secret manager and never place it in source,
client settings, or a committed `.env` file. Configure
`TWCLONE_FEEDBACK_REPOSITORY` for the public target (default
`rdearman/twclone`). The GitHub App needs only Issues read/write access to that
repository.

The worker stores only a generic sanitised publication error. It retries with
bounded exponential backoff and reclaims stale `processing` rows after a
worker restart. Each issue body contains the stable report UUID. Before retrying
an ambiguous create, the worker reconciles against GitHub Search using that
marker, preventing routine duplicate issue creation. GitHub's API does not
provide transactional exactly-once delivery, so reconciliation remains part of
the retry contract.

HTTP `Retry-After` and exhausted-rate-limit reset headers influence the retry
delay. Delays are bounded to one hour. Raw GitHub error response bodies are
discarded; only a generic retry status is persisted.

Required labels are created when permitted and existing labels are reused.
If label creation/listing is denied, the worker still attempts issue creation
without unavailable labels. GitHub publication can be disabled while reports
continue to queue locally.

## Operations

Queue-only mode is the default. To enable publication, provide the App ID,
installation ID, private-key file, repository, and explicit enable switch to
the server container. Monitor `publication_state`, `attempt_count`,
`next_retry_at`, and the generic `last_publication_error` field. Never log or
display the GitHub App JWT, installation token, private key, raw API response,
or report diagnostic buffer. Do not use the mock publisher tests against the
public GitHub endpoint.

## Verification status

The current acceptance report is
[`docs/testing/V3_PLAYER_FEEDBACK_ACCEPTANCE_20261114.md`](testing/V3_PLAYER_FEEDBACK_ACCEPTANCE_20261114.md).
The Python report workflow and PostgreSQL persistence/ownership paths were
exercised against a disposable live server. Mock publication and retry behavior
are automated. Godot runtime verification and live GitHub publication remain
separate, unverified items when their runtime/credentials are unavailable.
