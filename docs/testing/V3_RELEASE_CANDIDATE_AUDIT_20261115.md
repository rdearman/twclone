# V3 release-candidate audit — 15 November 2026

## Decision

**Not cleared for external beta deployment.** Gameplay integration evidence is
strong for the exercised PostgreSQL, Python and Godot workflows. Deployment
authorization, credential rotation, host network exposure and the designated
QA database remain unresolved in the latest security record. This audit does
not contact production or host PostgreSQL and does not modify beta or production
infrastructure.

Audit baseline: commit `d453d1165e77d6194ce4ac5bac4ba5a4ed82d6dd`
(`Verify StarPort construction replay after restart`). The worktree already
contained modified `Makefile` and `config.status`, deleted
`tests.v2/regression_report.json.tmp`, and untracked
`docs/V3_PLAYABILITY_AUDIT_20261113.md`. These were preserved; the deleted
report was not recreated, staged or committed.

## Checks completed in this audit

### StarPort scheduled deliveries and rollback

Built `twclone-rc-audit-20261115` from the current source, initialized a fresh
PostgreSQL 16 database with BigBang and migrations through 189, and ran the
serving V3 process in a task-created Docker network. The server was published
only on `127.0.0.1:12783`; PostgreSQL had no published host port. The isolated
stack was named `twclone-rc-audit-*`. It did not reuse the beta containers,
production services or host PostgreSQL.

An ordinary account registered with `auth.register` through the Python
client. After deterministic SQL fixture setup, the actual Python
`sector_info_flow`, `land_flow` and `planet_port_construct_flow` handlers
started construction 1 for planet 152 in sector 151. Initial construction
accounting left 9,900,000 credits and 1,000 each of ORE, ORG and EQU on the
planet from the 10,000,000-credit and 2,000-each fixture.

For deterministic scheduling, the disposable fixture shortened the delivery
interval to five seconds and moved only the disposable construction and
scheduler due timestamps forward. Every delivery was processed by the normal
server scheduler; the game server was restarted between deliveries.

| Operation | Result | Authoritative database evidence |
|---|---|---|
| First delivery after serving-process restart | PASS | `deliveries_completed=1`, status `building`; ORE/ORG/EQU each 1,000→900. |
| Injected second-delivery failure | PASS rollback | A disposable `BEFORE UPDATE` trigger raised on the EQU debit, after ORE and ORG had been updated in the same transaction. After restart and scheduler attempt, progress remained 1 and all three quantities remained 900. |
| Retry after removing failpoint and restarting | PASS | Progress advanced once to 2; ORE/ORG/EQU each became 800. |
| Third delivery after serving-process restart | PASS | Construction completed at delivery 3; one active port, ID 190, appeared in sector 151. Planet ORE/ORG/EQU each became 700. |
| Repeated restart after completion | PASS | Ordinary Python authentication plus `player.my_info` and `ship.status` succeeded. SQL still showed delivery 3, `completed`, port 190, one active sector port, and 700 of each material; restart did not replay a completed delivery. |
| Construction persistence rollback | PASS | A second ordinary Python `planet_port_construct_flow` was attempted with a disposable `BEFORE INSERT` trigger on `planet_port_constructions`. The workflow received a generic error. Credits remained 9,900,000; second-planet ORE/ORG/EQU remained 2,000 each; construction rows for that planet remained zero. |

The scheduler uses `FOR UPDATE SKIP LOCKED`; delivery material debits,
progress, port rows, stock, and completion state are within the delivery
transaction. This run observed the recovery behavior, but did not collect
PostgreSQL transaction IDs or inject a failure at every individual SQL
statement. Existing injected upgrade and destruction/stock rollback results
remain documented in
[`V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`](V3_GODOT_STARPORT_ACCEPTANCE_20261114.md).
The combined evidence supports the tested StarPort lifecycle paths; it is not
an exhaustive proof of every possible database failure boundary.

### Deployment and recovery controls reviewed

| Area | Status | Evidence and remaining limitation |
|---|---|---|
| Local container configuration | PASS (local configuration only) | `compose.beta.yaml` separates migration/runtime roles, keeps PostgreSQL on an internal network, publishes the game listener to loopback, requires TLS, and configures read-only/non-root runtime hardening. Historical local Compose startup and TLS/auth acceptance are recorded in `SECURITY_PUBLIC_BETA.md`. This is not a check of any external deployment. |
| Fresh PostgreSQL initialization | PASS (disposable) | Current-source Docker build and clean BigBang completed against PostgreSQL 16; migrations through 189, including feedback outbox 188 and trade idempotency 189, were applied. Upgrade from a deployment's existing schema and downgrade/rollback migrations were not run. |
| Player authentication | PASS (disposable) | Ordinary public registration, login after multiple serving-process restarts, and authenticated player/ship refreshes succeeded. Password hashing and SysOp negative tests have prior local evidence in `SECURITY_PUBLIC_BETA.md`. |
| Runtime database identity | PARTIAL | Compose configuration uses `twclone_runtime`; the previously documented hardened Compose run verified this role was non-superuser and lacked role/database creation privileges. This audit's custom disposable server used a test-only root container entrypoint and did not repeat the `current_user` least-privilege assertion. Do not use that custom run as proof of runtime hardening. |
| Feedback persistence and redaction | PASS for documented test scope | The feedback acceptance report records live disposable PostgreSQL Python preview/submit/status/ownership, planted-secret redaction, rollback, restart persistence and queue-only behavior. Mock GitHub creation/reconciliation/retry tests passed. No live GitHub App credentials were configured and no public issue was created. |
| Database backup recovery | PARTIAL | A local disposable `pg_dump`/restore check is documented in `SECURITY_PUBLIC_BETA.md`; no off-host backup restoration or operational recovery exercise is evidenced. |
| External network/firewall | BLOCKED | The security record states host PostgreSQL listeners on non-loopback addresses had unverified ownership/exposure and host firewall inspection was permission-blocked. No host inspection or connection was attempted in this audit. |
| Designated QA environment | BLOCKED | The latest security record reports the QA PostgreSQL cluster stopped and the runner's SCRAM credentials rejected. No QA or production service was contacted in this audit. |
| Credential rotation | BLOCKED | The security record documents an administrative credential previously exposed in local configuration and requiring owner-led rotation after the target is confirmed. No credential or endpoint was read, contacted or changed in this audit. No later evidence in the audited tree closes this item. |
| Per-player abuse control | PARTIAL | Feedback has per-player/global limits. The general game request limiter remains per connection, so reconnecting can obtain another allowance; shared authenticated-player/IP rate limiting remains a public-beta hardening item. |
| MySQL runtime | NOT CLAIMED | Schema/migration parity checks are not evidence of V3 server runtime support on MySQL. The verified release target here is PostgreSQL. |

## Remaining blockers ranked by impact

### P0 — deployment authorization and exposure

1. Confirm ownership and intended environment for the configured database
   target, rotate the previously exposed administrative credential through
   its authorized owner, and replace any reused application credential.
2. Establish host firewall and PostgreSQL listener ownership/exposure. Keep
   external exposure disabled until this review is complete.
3. Restore and authenticate the designated QA environment with protected
   credentials, then execute the deployment-specific release suite. No
   external deployment readiness can be inferred from the disposable stack.

### P1 — release operations

1. Verify the deployed game uses the least-privilege runtime DB role and
   protected credentials; repeat the identity/permission assertion on the
   exact release image and target deployment.
2. Test an off-host backup restore, monitoring/alerting, incident response,
   and the migration recovery procedure. Migration downgrade safety is not
   established.
3. Add a shared authenticated-player or IP request-rate limit before an
   unattended public beta if abuse exposure is in scope.

### P2 — optional operational integration

1. GitHub publication remains unverified without the authorized GitHub App
   installation and server-side credentials. Local report collection and
   PostgreSQL queue persistence work; live publication is not required to
   prove gameplay correctness if operators can review the local queue.
2. MySQL runtime support is not established and must not be advertised based
   on schema-parity checks.

## Beta position

The latest recorded gameplay gates report 2,341 server cases passed, Python
807 passed, Godot 4.5.2 104 passed/0 failed/1 skipped, and a live Godot
journey. The StarPort scheduler/restart and additional rollback cases in this
audit close named acceptance gaps. These results support a supervised
playtest only in an explicitly approved, isolated environment with local
feedback queue review. They do **not** clear deployment to external players.

Live GitHub publication is an operational integration and remains optional
when queue review is staffed. Credential rotation, deployment target
authorization, host exposure review and QA-backed release verification remain
release blockers.
