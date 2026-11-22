# Operations

This is the operational entry point for local administration, security and deployment readiness. External deployment and cutover procedures are separate from gameplay parity and require their own authorization.

## V3 beta candidate (prepared, not deployed)

The candidate profile is [`docker/bigbang.beta.json`](../docker/bigbang.beta.json)
and the isolated PostgreSQL/runtime stack is [`compose.beta.yaml`](../compose.beta.yaml).
The profile preserves the development `bin/bigbang.json`; it sets an exact
2,500-sector universe, 200 turns per day, a deterministic seed, density 4,
port ratio 40, and planet ratio 30%. The clean-install rehearsal produced
2,500 sectors, 750 initial planets, 1,019 ports, 101 NPCs, one Stardock and
Terra in sector 1. Seeded player rows were only the two internal SysOp actors;
known development player accounts (including `newguy`, `ai_qa_bot`, `ai_player`
and `BOT01`–`BOT10`) were absent. The two internal actors' seed passwords were
replaced with random Argon2id hashes during bootstrap. The runtime role had no
superuser, create-role or create-database flags.

This is a candidate configuration, not a balance approval. `planet_ratio=30`
is inherited from the former beta profile and creates 750 starting planets;
the development file's `0.01` is not comparable without checking generator
semantics. `max_total_planets=3000` permits growth beyond the 750 generated
worlds, while `max_planets_per_sector=6` and `genesis_block_at_cap=1` enforce
the configured per-sector cap. These materially affect world density and
Genesis availability and require the game owner to approve them before an
external beta. `startingcredits=5000` is explicit. No other economy rates,
prices or reward values were tuned. The development file's `turnsperday=999999`
is deliberately not copied.

New accounts receive the configured 200 turns at registration. Existing
accounts retain their current balance until the next scheduled daily reset;
the reset reads the persisted `config.turnsperday` and sets the daily amount
for all players. The BigBang JSON is initialization-only: editing it does not
change an already initialized database. A later policy change must update the
persisted config through a reviewed migration or authorized SysOp operation;
only then will the next daily reset apply the new amount. The public server
protocol contains no turn-grant command; the AI
playtest grant utility is an isolated administrative tool and is not included
in this runtime image.

### Candidate commands

Prepare `/etc/twclone-beta` on the deployment host and provision independent
database secrets. The game certificate is
`game.twclone.impara.co.uk`; it must be copied from Certbot's live lineage to
`game.crt` (root:root 0644) and `game.key` (root:10001 0640). The Compose
runtime is UID/GID 10001 and can read the key through its group without making
it world-readable. Keep the source key root-only and the containing secret
directory root-owned mode 0700. The environment file itself should be mode
0600. Never copy development credentials or place secret values in it.

```sh
install -d -o root -g root -m 0700 /etc/twclone-beta/secrets
umask 077
openssl rand -hex 32 > /etc/twclone-beta/secrets/postgres-admin.password
openssl rand -hex 32 > /etc/twclone-beta/secrets/runtime-db.password
printf 'db:5432:postgres:twclone_admin:%s\n' "$(cat /etc/twclone-beta/secrets/postgres-admin.password)" > /etc/twclone-beta/secrets/migrator.pgpass
printf 'db:5432:twclone:twclone_admin:%s\n' "$(cat /etc/twclone-beta/secrets/postgres-admin.password)" >> /etc/twclone-beta/secrets/migrator.pgpass
printf 'db:5432:twclone:twclone_runtime:%s\n' "$(cat /etc/twclone-beta/secrets/runtime-db.password)" > /etc/twclone-beta/secrets/runtime.pgpass
chown root:10001 /etc/twclone-beta/secrets/*
chmod 0640 /etc/twclone-beta/secrets/*
install -o root -g root -m 0644 /etc/letsencrypt/live/game.twclone.impara.co.uk/fullchain.pem /etc/twclone-beta/secrets/game.crt
install -o root -g 10001 -m 0640 /etc/letsencrypt/live/game.twclone.impara.co.uk/privkey.pem /etc/twclone-beta/secrets/game.key
```

Set the six `*_FILE` variables from `.env.beta.example` to those files and the
CA-issued certificate/key, then protect the environment file. Review the
rendered Compose configuration without publishing its values. For an approved
host, stage the source at a versioned path, build and record the image digest,
and then use:

```sh
docker compose -p twclone-beta -f compose.beta.yaml --env-file /etc/twclone-beta/.env.beta up -d --build
docker compose -p twclone-beta -f compose.beta.yaml --env-file /etc/twclone-beta/.env.beta ps
docker compose -p twclone-beta -f compose.beta.yaml --env-file /etc/twclone-beta/.env.beta logs --tail=100 game
docker compose -p twclone-beta -f compose.beta.yaml --env-file /etc/twclone-beta/.env.beta restart game
docker compose -p twclone-beta -f compose.beta.yaml --env-file /etc/twclone-beta/.env.beta stop
```

The game listener is intended for direct DNS-only TCP/TLS on port 12483.
Only the website hostname is HTTP-proxied by Cloudflare. Do not put the raw
game socket behind Cloudflare's normal HTTP proxy or route it through Nginx.
The C server loads its certificate during startup and does not reload TLS on
SIGHUP. Install `deploy/certbot/twclone-beta-game-deploy.sh` as a Certbot deploy
hook after the Compose service is installed; it atomically copies renewed
material with the key mode above and restarts `twclone-beta.service`. The hook
must be tested on the approved deployment host before enabling renewal.

The migration is a one-shot dependency of `up`; do not run it manually and
then call `up` against the same nonempty database. BigBang intentionally refuses
to initialize an already populated database. The Compose project publishes
game TCP only on loopback by default. For the dedicated beta host, set
`TWCLONE_BETA_HOST_ADDRESS=0.0.0.0`, retain the DNS-only
`game.twclone.impara.co.uk` record, and restrict the public port to the game
protocol in the host firewall. PostgreSQL remains private and has no host
port. Godot defaults to the public hostname, port 12483 and verified TLS; the
Python client supports verified TLS with `--tls` and optional `--ca-cert`.
The website's HTTP proxy does not carry the raw game protocol.

`deploy/systemd/twclone-beta.service` is a prepared system service wrapper;
install and enable it only after the host review. Compose applies container
restart policies. Compose rotates local-driver logs at 20 MiB per file (five
files for database/game and three for migration). The game health check
completes a TLS handshake, while PostgreSQL has a separate `pg_isready`
check; neither is an authenticated gameplay health check. Monitor both
container health states, logs, database readiness and a separate
ordinary-player smoke action.

### Backup, restore and rollback

Install `deploy/backup/twclone-beta-backup.sh` as
`/usr/local/sbin/twclone-beta-backup`, and enable
`deploy/systemd/twclone-beta-backup.timer`. The timer runs daily at 03:30 UTC
with up to ten minutes of jitter. Backups are custom-format dumps stored
root-only under `/var/backups/twclone-beta`, retained for 14 days, checksummed,
and verified with `pg_restore --list` before being accepted. Copy verified
backups to an approved off-host location as an additional recovery measure;
the local schedule alone does not protect against loss of the Netcup host.

For a manual backup, the service may be run with:

```sh
systemctl start twclone-beta-backup.service
journalctl -u twclone-beta-backup.service --since today
sha256sum -c /var/backups/twclone-beta/twclone-beta-*.dump.sha256
```

Restore only during an approved maintenance window after stopping the game and
taking a second backup of the current database. Restore into a fresh database
first, validate counts and login/gameplay, then switch the reviewed database
target; never overwrite the only recovery copy. Reapply
`docker/grant-runtime-privileges.sql` after a restore if role grants are not
present, restart the game, and verify ordinary login and persisted state. For
an application rollback, stop the game, retain the database, restore the exact
previous image digest, start it, and run the smoke journey. Do not roll back
schema migrations with an older binary unless that migration has a tested
down/restore path. `compose down` preserves the named volume; never use
`down -v` for an operational deployment.

### Readiness snapshot (2026-11-22)

| Gate | Result | Evidence / blocker |
|---|---|---|
| Candidate 2,500-sector / 200-turn clean BigBang | PASS | Disposable fresh-volume Compose rehearsal; exact sector count and bootstrap counts verified. |
| Development users/sessions/assets excluded | PASS for listed accounts | Fresh DB had no `newguy`, `ai_qa_bot`, `ai_player`, `BOT01`–`BOT10`; no prior sessions/assets were imported. Internal System/Federation actors remain as fresh server-owned seed rows with random locked passwords. |
| PostgreSQL runtime role separation | PASS | Fresh role is non-superuser and cannot create roles/databases; database/network are private to Compose. |
| Python suite | PASS | 834 passed with local socket access; Python client supports verified TLS via `--tls` and `--ca-cert`. |
| Godot 4.5.2 offline suite | PASS | Official 4.5.2 suite: 106 passed, 0 failed, 8 live-server scripts skipped; live TLS acceptance is run separately. |
| `tests.v2` unit suite | PASS | Daily-reset and beta-bootstrap contract tests passed against the configured 200-turn candidate. |
| Full 2,500-sector disposable PostgreSQL regression | PASS | 3,017 passed, 0 failed, 4 skipped; 176/177 suites executed, 1 intentionally skipped TLS suite; 0 unexecuted/blocked. The TLS suite requires its dedicated TLS-enabled run, tracked separately below. |
| Dedicated TLS smoke suite | PASS | TLS smoke: 4/4 passed. |
| Godot live TLS journey | PASS on disposable candidate | Godot 4.5.2 registered, navigated, scanned, bought and sold through menus, logged out, and reconnected after server restart. Candidate used an isolated test certificate, not the public Netcup hostname. |
| Python live TLS login / registration | PASS on disposable candidate | Python CLI registered over verified TLS, warped to sector 2, and bought two ORG through its menu; authoritative `trade_log` records the transaction. |
| 200-turn allocation/reset | PASS on disposable candidate | Persisted setting was 200; actual scheduler reset a player from 7 to 200 turns at the due time. |
| Backup/restore operational rehearsal | PARTIAL | Prior local custom-format restore exists; no off-host restore run for this candidate. |
| Firewall/host ownership/admin QA account/off-host TLS | NOT TESTED | Requires the actual deployment owner and environment-specific approval. |
| Beta readiness | DEPLOYMENT GATES REMAIN | Candidate gameplay/configuration gates pass. Netcup hardening, website origin HTTPS, scheduled backup verification and external acceptance must be completed before announcing the public beta. |

## Release boundary

This section covers the dedicated Netcup beta only. It does not authorize changes to
Impara application infrastructure, Cloudflare zone-wide TLS settings, or the LAN
AI playtest. The game and static website use separate listeners and services.

- [Public beta security baseline](SECURITY_PUBLIC_BETA.md)
- [V3 deployment hardening plan](V3_DEPLOYMENT_HARDENING_PLAN.md)
- [Release-candidate audit](testing/V3_RELEASE_CANDIDATE_AUDIT_20261115.md)
- [SysOp menu contract](SYSOP_MENU.md)
- [Password hashing and authentication](PASSWORD_HASHING.md)
- [Database backend preparation and validation](V3_MYSQL_BACKEND.md)
- [Player feedback system and queue operations](V3_PLAYER_FEEDBACK_SYSTEM.md)
- [Feedback protocol](PROTOCOL.v3/31_Player_Feedback.md)

Do not infer deployment readiness from passing local gameplay suites. The current gameplay release evidence is in [V3 parity](V3_PARITY.md).
