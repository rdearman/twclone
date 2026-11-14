# Player feedback commands

Player feedback is an authenticated support feature. It does not consume turns,
credits, cargo, or other gameplay resources. The PostgreSQL V3 server stores
reports locally even when GitHub publication is disabled or unavailable.

All requests use the normal V3 envelope and authenticated session metadata.
Clients must redact diagnostics before buffering; the server independently
filters diagnostics and free text before persistence.

## `feedback.preview`

Preview is required before submitting a report. The response title and body are
the exact public issue content persisted by `feedback.submit` for that
idempotency key.

Request `data` fields:

| Field | Type | Required |
|---|---|---|
| `report_type` | `bug` or `suggestion` | yes |
| `subject` | string, at most 160 bytes | yes |
| `description` | string, at most 12,000 bytes | yes |
| `expected_behavior` | string | bugs |
| `actual_behavior` | string | bugs |
| `reproduction_steps` | string | no |
| `diagnostics` | JSON | no |
| `client_name` | `python` or `godot` | yes |
| `client_version`, `build_id`, `protocol_version` | bounded strings | no |
| `idempotency_key` | UUID | yes |

Successful response data contains `report_id`, `title`, and `body`. Preview
does not write a report or consume a gameplay resource.

## `feedback.submit`

Uses the same fields and the same UUID as its preview. Bug reports require
expected and actual behaviour. Server-side validation, allowlist sanitisation,
size limits, per-player/global hourly limits, and transaction persistence are
applied before success is returned.

Successful data contains `report_id`, `state`, and `idempotent`; it includes
`github_issue_number` and `github_issue_url` once published. Repeating the same
UUID for the same player returns the existing report and does not create a new
outbox item.

## `feedback.status`

Request: `{"report_id":"<uuid>"}`. Successful data includes `report_id`,
`state`, and `created_at`, with GitHub issue fields after publication. The
server scopes lookup to the authenticated owner; unknown and other-player IDs
are indistinguishable.

## `feedback.diagnostics`

Request: `{"report_id":"<uuid>"}`. Returns only the already sanitised
diagnostic object attached to a report owned by the authenticated player. It
does not expose server logs or other players' activity.

## Publication state

States are `pending`, `processing`, `published`, `retryable`, and `failed`.
The outbox worker is enabled only when GitHub App publication configuration is
present. Otherwise rows remain `pending` for queue-only operation. Retries use
bounded backoff. Before creating an issue after an ambiguous result, the worker
searches the public issue body for the stable report UUID marker.

The public issue uses `[V3][Python|Godot][Bug|Suggestion] <subject>` and the
labels `player-bug` or `player-suggestion`, `client-python` or `client-godot`,
`v3-playtest`, and `needs-triage` when those labels are available.
