# Restricted Public Beta Security Baseline

This document records security controls implemented for a restricted beta and
distinguishes source/build evidence from runtime verification. It does not
authorize public exposure or production deployment.

## Player credentials

Player passwords are stored as Argon2id encoded hashes, with per-password
random salts and the current parameters (64 MiB, three passes, one lane).
Registration and password changes call the same hashing implementation;
authentication verifies the encoded hash. PostgreSQL migration 119 enforces
the encoded Argon2id format. Relevant code is in `src/password_hash.c` and
`src/db/repo/repo_auth.c`. Focused password/migration tests are listed in the
security verification record. This is distinct from PostgreSQL connection
credentials.

## Listener and request limits

The player listener defaults to `127.0.0.1`. Configure `server_bind_address`
or `TWCLONE_LISTEN_ADDRESS` explicitly. A deployment that previously relied on
the historical all-interface bind must set `0.0.0.0` deliberately; this is a
compatibility migration, not an implicit public-beta default. The beta Compose
file binds the host publication to `127.0.0.1` only.

Limits are configurable in bootstrap JSON or through the corresponding
environment settings:

| Setting | Default | Supported range |
| --- | ---: | ---: |
| `server_max_connections` / `TWCLONE_MAX_CONNECTIONS` | 128 | 1–10,000 |
| `server_max_request_bytes` / `TWCLONE_MAX_REQUEST_BYTES` | 65,536 | 256–1,048,576 |
| `server_request_timeout_ms` / `TWCLONE_REQUEST_TIMEOUT_MS` | 10,000 | 100–300,000 |
| `server_requests_per_minute` / `TWCLONE_REQUESTS_PER_MINUTE` | 200 | 1–100,000 |

Frames are newline-delimited and rejected when oversized or incomplete at the
deadline. Request rate is actively rejected, not just reported in response
metadata. The current limiter is per connection; clients can obtain another
allowance by opening a new connection, bounded by the connection cap. A
shared per-IP or authenticated-player limiter remains a beta hardening item.
TLS handshakes run in bounded connection workers rather than the accept loop.
TLS 1.2 or later is required when TLS is enabled.

## SysOp authorization

SysOp access is determined by `auth_player_is_sysop()` and all SysOp handlers
use that policy. JSON negative cases cover ordinary-player attempts to list or
change configuration and kick a player. The local TLS runtime check exercised
all three denials against an ordinary authenticated player; each returned
`status=refused`, code `1407`.

## Database roles and secret handling

The beta Compose deployment separates migration from game runtime. Migrations
run with `twclone_admin`; the game process connects as `twclone_runtime`, a
non-superuser role granted application DML and sequence/function access only.
Credentials and TLS keys are mounted as secrets and copied to mode-0600
temporary passfiles under `/tmp`. PostgreSQL is attached only to an internal
Compose network; the game joins a separate bridge only for the host-published
player listener. The player port is published on loopback, PostgreSQL has no
host port binding, and the S2S listener binds to container loopback only.

The ignored local `bin/bigbang.json` previously contained inline passwords in
both the admin and app connection strings. After the owner cleared its
immutable (`i`) attribute, this session removed those inline values, configured
both libpq connections to use `/home/rick/.pgpass`, and set the file mode to
0600. A redacted structural check confirmed there is no `password=` parameter
and both entries specify the passfile. No database connection was made to
validate those credentials.

The credential previously embedded in the `admin` entry still requires
rotation because it was exposed in local configuration. The configured host is
not the disposable Compose database. Its production status and ownership have
not been established, so this session did not contact it or rotate a password.
The database owner must confirm the target and perform/authorize rotation. The
app credential should also be rotated if valid or reused.

The owner action that was required to clear the immutable attribute was:

```sh
sudo chattr -i /home/rick/Development/Active/twclone/bin/bigbang.json
```

Then remove the inline connection credentials from that local file and rotate
the configured PostgreSQL administrative/superuser credential through the
database owner's approved procedure. The app credential should also be
replaced if it is real or reused. This session did not contact the configured
database, change any credential, or reveal any secret. The Docker build
explicitly excludes `bin/bigbang.json`; its bootstrap config contains no
credentials, and the local beta deployment used separate randomly generated
admin/runtime secrets mounted from a private temporary directory.

## Container baseline

`Dockerfile` uses separate build and runtime stages. The game runs as UID/GID
10001 with a read-only root filesystem, dropped capabilities,
`no-new-privileges`, bounded memory/CPU/PIDs, and a private database network.
The image built successfully. A clean local Compose database applied the
canonical PostgreSQL migrations and BigBang completed; the migration job exited
0. The game started as UID 10001 with restart count 0 after correcting the
missing S2S-port fallback. The engine completed its S2S handshake and the player
listener bound `0.0.0.0:12483` inside its container. Docker published only
`127.0.0.1:12483`; no PostgreSQL or S2S host publication was present. This is a
local verification stack, not a public deployment.

## Executed verification (2026-11-12)

### Latest clean local-stack run

The following checks were run against the fresh disposable Compose project
`twclonebetafinal` (not the designated host QA cluster):

| Check | Result | Evidence |
| --- | --- | --- |
| Clean database initialization and migrations | PASS | `twclonebetafinal-migrate-1` exited 0; BigBang reported `OK: Big Bang Complete`, applied migrations through 181, and runtime grants completed. Container logs were inspected read-only. |
| Game process startup | PASS | `twclonebetafinal-game-1` running, exit 0, restart count 0; server log shows S2S on `127.0.0.1:4321`, engine accepted, TLS initialized, player listener on container `0.0.0.0:12483`. |
| Player TLS/authentication workflow | PASS | Against the loopback-published listener: `system.hello`, fresh disposable `auth.register`, `auth.login`, and authenticated `player.my_info` all succeeded. Exit 0; stdout/stderr/exit preserved at `/tmp/twclone-beta-local.txtuf7/final-gameplay.{stdout,stderr,exit}`. |
| Runtime database identity | PASS | Read-only query from the game container returned `current_user=twclone_runtime`, `rolsuper=false`, `rolcreatedb=false`, `rolcreaterole=false`; query also found zero `turns.turns_remaining IS NULL` rows in this fresh database. |
| Container/network properties | PASS (local configuration) | `docker inspect` showed UID/GID 10001, read-only root, all game capabilities dropped, `no-new-privileges`, 1 GiB/2 CPU/256 PID limits, only three secret-file mounts. Game publication is `127.0.0.1:12483`; DB has no host port binding and belongs only to internal `beta_private`; no S2S host port is published. |

The current stack's clean database did not reproduce the previously observed
`daily_turn_reset` null-value error: its null-row count is zero and no such
error appeared in the inspected current startup logs. The earlier occurrence
is not considered fixed or runtime-cleared by this observation; it remains an
unresolved follow-up until the scheduler has been exercised through a daily
reset on this clean database.

The six adversarial transport probes below were previously run against the
actual V3 server and a separate disposable local Compose PostgreSQL stack with
test limits. Their stdout, stderr and exit status are preserved in
`/tmp/twclone-beta-local.txtuf7/adversarial-local.{stdout,stderr,exit}`. They
are not results from the designated QA cluster on port 55438. The official
QA-backed runner is blocked by the credential failure recorded below, and no
full adversarial report from that runner is claimed.

| Check | Result | Evidence |
| --- | --- | --- |
| Sanitized C build (`make -j1`) | PASS | Exit 0; ASan/UBSan flags used for changed server objects and final link. |
| Docker image build | PASS | `docker build --no-cache -t twclone-beta:local .`, exit 0. |
| Fresh local PostgreSQL + migration job | PASS | Compose `up` exit 0; BigBang completed and runtime grants were applied. |
| TLS handshake scheduling | PASS | Adversarial probe: stalled handshake did not block a second TLS client. |
| Partial-frame deadline | PASS | Probe received timeout error code 505. |
| Oversized request | PASS | Probe received rejection code 405. |
| Malformed JSON handling | PASS | Error returned and same connection remained usable. |
| Request rate rejection | PASS | Fifth request on the four-request test limit was rejected with code 406. |
| Connection cap | PASS | Fourth active client held; excess TLS client could not complete. |
| SysOp authorization | PASS | Three ordinary-player attempts returned refusal code 1407. |
| Authentication/gameplay/persistence | PASS | TLS `auth.register` and authenticated `player.my_info` succeeded; account was visible through the runtime DB role. |
| Runtime DB least privilege | PASS | `twclone_runtime` has `rolsuper=false`, `rolcreatedb=false`, `rolcreaterole=false`; `CREATE ROLE` was denied and no role persisted. |
| Docker hardening | PASS | Runtime UID 10001, read-only root, all capabilities dropped, `no-new-privileges`, 1 GiB/2 CPU/256 PID limits; only secrets mounted in game container. |
| Network exposure | PASS (local only) | Host listener was `127.0.0.1:12483`; PostgreSQL had no published port; S2S was `127.0.0.1:4321` inside game container only. |
| Designated QA PostgreSQL port 55438 / official QA runner | BLOCKED | The bounded direct read-only `psql` probe to `127.0.0.1:55438`, user `twclone_qa_runner`, exited 2 with `FATAL: password authentication failed for user "twclone_qa_runner"`. The runner produced no usable report. No database was created or modified, and the same failed connection was not retried. |
| Local BigBang credential cleanup | PASS | Inline passwords removed; both connection entries now specify `/home/rick/.pgpass`; file mode 0600. |
| Previously exposed administrative credential rotation | BLOCKED | Configuration targets a non-Compose host whose production status is unconfirmed. No connection or rotation was attempted; owner confirmation/authorization is required. |
| Security commit | PASS | `d77d8e05` (`Harden V3 public beta deployment`). Corporation gameplay changes remain separate and uncommitted. |

Execution evidence (separate stdout/stderr and exit files) is retained for
local Docker runtime checks under `/tmp/twclone-beta-local.txtuf7/`. Test probes
used test-only values: 4 concurrent connections, 4,096-byte frame limit,
1,500 ms request deadline and 4 requests/minute. Compose configuration exposes
these settings as environment-overridable defaults; the deployment should use
the documented production limits, not the test overrides.

The QA credentials/artifacts must be kept distinct: the successful TLS and
runtime-role evidence is from the project-owned, disposable Docker database;
the designated port-55438 QA login is an independent blocked check. Host socket
inspection also showed PostgreSQL listeners on `100.70.35.84:5432`,
`192.168.25.5:5432`, and `127.0.0.1:5432`. No connection was attempted to any
of them. They are outside this Compose project and their ownership, firewall
exposure and production status were not verified. This is an environment-level
readiness risk, not a port published by this beta Compose file.

The local BigBang file previously contained inline connection passwords in the
`admin` and `app` entries; secret values are intentionally not reproduced
here. The administrative credential is the credential requiring owner-led
rotation. The local app credential should also be replaced if valid or reused.
The owner cleared the immutable bit using:

```sh
sudo chattr -i /home/rick/Development/Active/twclone/bin/bigbang.json
```

Inline passwords have now been removed from that local configuration. No
remote database or credential was accessed or modified; rotation remains
pending target confirmation and owner authorization.

An earlier local run observed the engine's `daily_turn_reset` task report a
PostgreSQL `NOT NULL` failure because a `turns` row had null
`turns_remaining`. The fresh database currently has no null rows and the error
did not recur in inspected startup logs, but the daily scheduler has not been
run through a reset. This remains unverified and should be exercised before
beta; it is separate from the security acceptance results.

## Beta decision

### Deployment-readiness matrix

| Requirement | Status | Evidence / remaining action |
| --- | --- | --- |
| No inline database credentials in local BigBang configuration | **PASS (file hygiene)** | Inline admin/app password parameters were removed; both conninfos now use `/home/rick/.pgpass`; file mode is 0600. The previously exposed admin credential still needs owner-authorized rotation. |
| Safe player listener default and restricted local publication | **PASS (local)** | Native default is loopback; Compose published only `127.0.0.1:12483`. Internet/firewall exposure was not tested. |
| Request size/deadline, connection cap and active rate rejection | **PASS (local probes)** | Oversize, partial-frame deadline, cap, and rate probes passed; see preserved 6/6 evidence. Rate limiting is per connection, so connection churn can obtain new allowance. |
| Slow TLS handshake isolation | **PASS (local probe)** | Stalled handshake did not prevent another TLS client completing. |
| Central SysOp authorization and negative escalation tests | **PASS (local)** | Three unauthorized commands were refused with code 1407; automated JSON negatives are present. |
| Docker non-root/read-only/restricted capabilities/resource limits | **PASS (local)** | Runtime inspect evidence: UID 10001, read-only root, dropped capabilities, `no-new-privileges`, bounded resources, secrets-only game mounts. |
| Migration/runtime role separation and runtime least privilege | **PASS (local)** | Migration job ran with admin identity; game SQL reports `twclone_runtime` with no superuser/createdb/createrole flags. |
| Database network boundary | **PASS (Compose topology)** | PostgreSQL is on internal `beta_private`, has no host port binding, and is not attached to `beta_frontend`. Host-owned PostgreSQL listeners elsewhere remain outside this verification. |
| Complete adversarial suite on designated QA cluster | **BLOCKED** | Port 55438 authentication failed for the configured QA runner; no full QA report exists. Equivalent six probes did pass against the local disposable Compose database/server. |
| Host firewall / external reachability | **BLOCKED** | Not tested; host socket inspection found unrelated PostgreSQL listeners on port 5432 on non-loopback interfaces. Their owner and exposure must be established without connecting to them. |
| Local clean migration and app startup | **PASS (local)** | Fresh Compose migration job exit 0; game running without restarts. Daily scheduler execution remains unverified. |
| Repository commit | **BLOCKED** | `.git` is mounted `ro`; the shared change set cannot be committed in this environment. |

**NO-GO for restricted public beta at this time.** The fresh isolated local
deployment verifies clean migration, TLS player authentication, game startup,
container hardening, loopback-only published player port, private DB topology,
and least-privilege application DB identity. The six transport probes and three
SysOp denial checks also passed on the actual local V3 server. Nonetheless, the
previously exposed admin credential requires owner-authorized rotation; the
designated QA runner cannot authenticate; host PostgreSQL listeners on port
5432 have unverified ownership/exposure; and there is no preserved complete QA
report. The daily reset issue has not been exercised on the current fresh
stack. Do not expose this stack publicly.

Before reconsidering even a restricted beta:

1. Confirm the configured database target is non-production or explicitly
   authorize its owner to rotate the previously exposed administrative
   credential; keep it out of runtime access. Rotate the app credential if it
   is valid or reused.
2. Restore valid designated-QA credentials and rerun the QA-backed suite with
   a preserved report.
3. Exercise the daily turn reset on a clean QA database; resolve the earlier
   null `turns_remaining` error if it reproduces.
4. Establish ownership and firewall exposure for host PostgreSQL listeners
   without connecting to or modifying production services; keep public network
   exposure disabled until separately authorized.
5. Complete migration, gameplay regression, backup/restore, monitoring and
   incident-response review before any broader release.
