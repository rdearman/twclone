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

As of 2026-11-13, the ignored native `bin/bigbang.json` app connection names
`twclone_runtime`; its separate `admin` connection still names `postgres` for
BigBang administration. This was a local-only configuration edit; no host was
contacted. The current `.pgpass` has no confirmed matching runtime-role entry
for the configured target, so native runtime authentication is not verified
and will fail closed until the database owner provisions a least-privilege
runtime credential. BigBang migrations must receive their separately
authorized migration connection through `TWCLONE_DB_APP_CONNINFO`; do not
reuse the admin connection for the running game.

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

## Executed verification (2026-11-12; follow-up 2026-11-13)

### Latest clean local-stack run

The following checks were run against the fresh disposable Compose project
`twclonebetafinal` (not the designated host QA cluster):

| Check | Result | Evidence |
| --- | --- | --- |
| Clean database initialization and migrations | PASS | `twclonebetafinal-migrate-1` exited 0; BigBang reported `OK: Big Bang Complete`, applied migrations through 181, and runtime grants completed. Container logs were inspected read-only. |
| Game process startup | PASS | `twclonebetafinal-game-1` running, exit 0, restart count 0; server log shows S2S on `127.0.0.1:4321`, engine accepted, TLS initialized, player listener on container `0.0.0.0:12483`. |
| Player TLS/authentication workflow | PASS | Against the loopback-published listener: `system.hello`, fresh disposable `auth.register`, `auth.login`, and authenticated `player.my_info` all succeeded. Exit 0; stdout/stderr/exit preserved at `/tmp/twclone-beta-local.txtuf7/final-gameplay.{stdout,stderr,exit}`. |
| Runtime database identity | PASS | Read-only query from the game container returned `current_user=twclone_runtime`, `rolsuper=false`, `rolcreatedb=false`, `rolcreaterole=false`; query also found zero `turns.turns_remaining IS NULL` rows in this fresh database. |
| Local database backup/restore | PASS | In the positively identified `twclonebetafinal` internal DB container, `pg_dump -Fc` completed, restore into a temporary database completed with `pg_restore --exit-on-error`, and player/port/table/function counts matched (106/216/227/106). The temporary restore database and private dump were removed after comparison. This is not a tested off-host recovery. |
| Container/network properties | PASS (local configuration) | `docker inspect` showed UID/GID 10001, read-only root, all game capabilities dropped, `no-new-privileges`, 1 GiB/2 CPU/256 PID limits, only three secret-file mounts. Game publication is `127.0.0.1:12483`; DB has no host port binding and belongs only to internal `beta_private`; no S2S host port is published. |

This baseline section records the original clean-start observation. The daily
reset was subsequently exercised twice on this same isolated stack; results
and the reproduced earlier failure are recorded in the 2026-11-13 update below.

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
| Designated QA PostgreSQL port 55438 / official QA runner | BLOCKED | `pg_lsclusters` currently reports PostgreSQL 16 `twclone_qa` on port 55438 as down. No service was started and no connection retried. The last recorded runner rejection is preserved in the cluster log. |
| QA authentication diagnosis | BLOCKED (credential mismatch) | `/var/log/postgresql/postgresql-16-twclone_qa.log` records at 2026-11-12 20:41:15 GMT: `FATAL: password authentication failed for user "twclone_qa_runner"`; detail says the connection matched `/etc/postgresql/16/twclone_qa/pg_hba.conf` line 125, `host all all 127.0.0.1/32 scram-sha-256`. Thus TCP reached PostgreSQL and matched an authentication rule; the failure was SCRAM credential validation, not a missing HBA allowance. The cluster is now down, so current effective rules cannot be queried through PostgreSQL. No password or database state was changed. |
| Local BigBang credential cleanup | PASS | Inline passwords removed; both connection entries now specify `/home/rick/.pgpass`; file mode 0600. |
| Previously exposed administrative credential rotation | BLOCKED | Configuration targets a non-Compose host whose production status is unconfirmed. No connection or rotation was attempted; owner confirmation/authorization is required. |
| Corporation member asset transfers | PASS (focused prior runtime evidence) | `485fc0b5` contains the separate feature commit; focused JSON acceptance passed 31/31 against isolated PostgreSQL on 2026-11-12, including concurrent duplicate-transfer conservation. |
| Daily turn reset, configured value | PASS (Docker scheduler) | On 2026-11-13 the actual engine scheduler ran twice against `twclonebetafinal` PostgreSQL 16.15. With `turnsperday=120`, five rows reset from 7 and then 3 to 120; both `last_run_at` values advanced, with zero mismatches/NULLs and no reset SQL error. |
| Daily turn reset, missing-config fallback | PASS (rebuilt Docker runtime) | Image `twclone-beta:resetcheck` (image ID `sha256:51fae3247850df8e32b15f2c7422ac6adbe676ddcec84d00512e02d1537a292b`) ran as UID 10001 with a read-only root filesystem and no published ports on the internal QA network. On a disposable clone of the Docker QA database, `turnsperday` was deleted and all five `turns` rows set to 11; only `daily_turn_reset` was enabled and scheduled due. The server started, logged `Listening on 127.0.0.1:12484`, and PostgreSQL showed five rows at 120, zero `turnsperday` rows, and `last_run_at=2026-11-13 09:45:49+00`. The test container and clone database were removed afterward; the source QA database was not modified. |
| Configured listener bind | PASS (rebuilt Docker runtime) | The same un-published test container honored `TWCLONE_LISTEN_ADDRESS=127.0.0.1` and `TWCLONE_LISTEN_PORT=12484`; startup log confirmed `Listening on 127.0.0.1:12484`. The remaining source diff is the matching function parameter declaration; review/build/runtime checks passed, but commit was blocked because `.git/index` is read-only in this session. |
| Security commit | PASS | `d77d8e05` (`Harden V3 public beta deployment`). |

Execution evidence (separate stdout/stderr and exit files) is retained for
local Docker runtime checks under `/tmp/twclone-beta-local.txtuf7/`. Test probes
used test-only values: 4 concurrent connections, 4,096-byte frame limit,
1,500 ms request deadline and 4 requests/minute. Compose configuration exposes
these settings as environment-overridable defaults; the deployment should use
the documented production limits, not the test overrides.

The QA credentials/artifacts must be kept distinct: the successful TLS and
runtime-role evidence is from the project-owned, disposable Docker database;
the designated port-55438 QA login is an independent blocked check. Read-only
inspection found the host `twclone_qa` cluster stopped; its retained log shows
SCRAM password validation failure for the prior runner attempt. Host socket
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

The reset failure was reproduced from retained engine logs as SQLSTATE `23502`
because a missing `turnsperday` lookup supplied NULL to the turn update. The
handler now uses the documented 120 fallback and the repository binds that
validated value. Beyond static tests and configured-value scheduler runs, the
rebuilt image was exercised against an isolated disposable clone with the
config row absent; five players were reset from 11 to 120 and the cron
`last_run_at` advanced. The clone and test container were cleaned up.

### Host administrator read-only inspection commands

Run these on the host as an authorized administrator. They inspect only; they
do not alter firewall or PostgreSQL configuration:

```sh
sudo nft list ruleset
sudo ufw status verbose
sudo iptables -S INPUT
sudo ss -ltnp '( sport = :5432 )'
sudo -u postgres psql -X -d postgres -c 'SHOW listen_addresses;'
sudo -u postgres psql -X -d postgres -c \
  "SELECT rolname, rolsuper, rolcreatedb, rolcreaterole, rolreplication, rolbypassrls FROM pg_roles ORDER BY rolname;"
sudo -u postgres psql -X -d postgres -c \
  "SELECT datname, pg_get_userbyid(datdba) AS owner, datallowconn FROM pg_database ORDER BY datname;"
```

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
| Migration/runtime role separation and runtime least privilege | **PASS (Docker); BLOCKED (native runtime auth)** | Docker game SQL reports `twclone_runtime` with no superuser/createdb/createrole flags. Native `bin/bigbang.json` now defaults app access to `twclone_runtime`, not `postgres`; a matching native passfile credential is not confirmed. BigBang's `postgres` admin connection remains migration-only and must not be used by the running game. |
| Database network boundary | **PASS (Compose topology)** | PostgreSQL is on internal `beta_private`, has no host port binding, and is not attached to `beta_frontend`. Host-owned PostgreSQL listeners elsewhere remain outside this verification. |
| Complete adversarial suite on designated QA cluster | **BLOCKED** | Port 55438 authentication failed for the configured QA runner; no full QA report exists. Equivalent six probes did pass against the local disposable Compose database/server. |
| Host firewall / external reachability | **BLOCKED** | Read-only `nft list ruleset` exited 1 (`Operation not permitted (you must be root)`); `iptables -S INPUT` exited 4 (permission denied); `ufw status verbose` requires root. Host socket inspection found PostgreSQL listeners on non-loopback addresses; ownership/exposure remains unknown. No firewall changes or DB connections were attempted. |
| Local clean migration and app startup | **PASS (local)** | Fresh Compose migration job exit 0; game running without restarts. Daily reset also ran twice through the actual scheduler as recorded above. |
| Corporation transfer commit separation | **PASS** | Gameplay feature commit `485fc0b5` is separate from the security commits. |

**NO-GO for restricted public beta at this time.** The fresh isolated local
deployment verifies clean migration, TLS player authentication, game startup,
container hardening, loopback-only published player port, private DB topology,
and least-privilege application DB identity. The six transport probes and three
SysOp denial checks also passed on the actual local V3 server. Nonetheless, the
designated QA cluster is currently stopped and its retained log shows the
runner password rejected under SCRAM; host PostgreSQL listeners on port 5432
have unverified ownership/exposure; no complete report exists from the
designated QA runner; native runtime credentials remain unverified; and the
previously exposed administrative credential still requires owner-authorized
rotation. The rebuilt image's missing-config daily-reset path has now passed on
a disposable Docker database clone. Do not expose this stack publicly.

Before reconsidering even a restricted beta:

1. Confirm the configured database target is non-production or explicitly
   authorize its owner to rotate the previously exposed administrative
   credential; keep it out of runtime access. Rotate the app credential if it
   is valid or reused.
2. Have the host administrator restore the designated QA cluster and reconcile
   the `twclone_qa_runner` SCRAM credential with the protected client
   credential; then rerun the QA-backed suite with a preserved report.
3. Establish ownership and firewall exposure for host PostgreSQL listeners
   without connecting to or modifying production services; keep public network
   exposure disabled until separately authorized.
4. Complete migration, gameplay regression, backup/restore, monitoring and
   incident-response review before any broader release.
