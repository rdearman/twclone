# V3 deployment hardening plan

Status: **PLAN ONLY — no credentials, services, firewall rules, accounts, or
deployments have been changed.**

This plan closes the three release blockers in
[`V3_RELEASE_CANDIDATE_AUDIT_20261115.md`](testing/V3_RELEASE_CANDIDATE_AUDIT_20261115.md).
Gameplay acceptance is treated as a separate completed workstream. The target
environment remains closed to external testers until the checks and approvals
below are complete.

## Safety and approval boundary

Before any mutation, record the named environment, system owner, change window,
operator, recovery contact, current release image, database identity, and
whether the target is production, beta, or disposable QA. Do not infer an
environment from a hostname, port, shell history, or credential file.

The following are inspection or staging work only. Each production or host
change requires the owner to approve the exact target and operation first:

- Rotating a database, QA-runner, TLS, GitHub App, or other service credential.
- Changing role grants, service identities, secret files, or credential
  distribution.
- Editing firewall, cloud security-group, router, proxy, DNS, or listener
  configuration.
- Creating, disabling, or changing QA/player accounts or permissions.
- Applying migrations, restoring a database, restarting a service, or
  deploying/reverting an image.

Approval for one operation does not authorize the others. Keep the environment
private until the external exposure checks pass. Never paste secrets into
tickets, chat, shell history, source control, command output, or this plan.

## 1. Credential inventory and rotation

### Prechecks

1. The environment owner identifies each credential principal, its target
   system, purpose, consumers, owner, and last rotation date. Inventory secret
   *locations and metadata*, not values: PostgreSQL admin/migration role,
   runtime role, designated QA runner, TLS private key/certificate, and any
   configured publisher credential.
2. Check ignored/local config, Compose or service-manager secret references,
   protected passfiles, CI secret stores, backup/export locations, and log
   retention. Search results must not print matched secret values. Record only
   file/service identity, permission, owner and rotation disposition.
3. Confirm the database target that used the previously exposed administrative
   credential. The credential must be considered compromised even if it is
   currently unused. Check for reuse of its value without revealing or
   comparing plaintext in logs.
4. Confirm the application role is not a superuser and cannot create roles or
   databases. Separate migration/admin use from game runtime. The game must
   authenticate only as the least-privilege runtime principal.
5. Establish an approved maintenance window, known-good release image, tested
   backup/restore point, and console or out-of-band access before changing
   credentials.

### Rotation sequence

1. Prove the procedure in the disposable PostgreSQL stack, then in the
   designated private QA environment. Generate new high-entropy credentials
   with the approved secret manager; do not generate or transmit production
   secrets in this repository or an interactive transcript.
2. Prefer a versioned replacement runtime principal for a controlled overlap:
   grant only the existing runtime permissions, update the private secret
   mount, restart one QA instance, and verify `current_user`, role flags,
   application login, gameplay reads/writes, scheduler work, and feedback
   queue access. If policy requires retaining the same role name, use the
   approved coordinated cutover instead.
3. Rotate the exposed administrative credential to a new migration-only
   secret. Do not restore the exposed value as a rollback option. Run the
   migration job with the replacement, verify its identity and expected
   privileges, then verify that the game still connects with the separate
   runtime principal.
4. Revoke old or unused credentials after all consumers are confirmed on the
   replacement. Remove obsolete secret copies from active configuration and
   revoke any related sessions/keys where supported. Keep required encrypted
   backups under the organization's retention policy; do not rewrite evidence
   or backups without the owner's retention decision.
5. Reconcile the designated QA runner with the protected client credential.
   Verify the runner is scoped to the QA database and cannot connect to
   production. Do not grant superuser, role creation, or database creation to
   make a test pass; identify the exact missing privilege and approve only the
   necessary QA-scoped grant.
6. Replace TLS material only if inventory or expiry checks require it. Verify
   certificate chain, SAN, key permissions, renewal ownership, and both sides
   of the configured TLS termination path.

### Verification and rollback

- Test the new migration and runtime credentials from their actual consumers.
  Record principal names and success/failure only, never secrets. Confirm the
  runtime role cannot perform administrative operations.
- Exercise ordinary-player registration/login and the agreed smoke journeys;
  check database scheduler and feedback queue health. Check old credentials
  fail after revocation.
- If runtime authentication fails, keep external access closed and roll
  forward by correcting the replacement secret/role grants. Do not re-enable
  the previously exposed admin credential. For a non-compromised runtime
  credential, a time-limited overlap may be used only if pre-approved and
  audited; revoke it immediately after recovery.
- If a migration credential fails, stop further migration/deployment work.
  Use the tested database restore or an approved forward repair. Do not
  improvise reverse SQL for migrations without a documented and tested
  downgrade.

## 2. Host ownership, network exposure, and service identity

### Read-only inventory

An authorized host administrator first records:

- Host and deployment owner; active Compose/systemd/orchestrator project and
  image digest; service UID/GID and file ownership.
- Listening sockets and owning processes, firewall rules, cloud security
  groups/load balancers, proxy routes, DNS records and externally reachable
  addresses.
- Intended public entry point and exact port allowlist. Identify whether TLS
  terminates at the game listener or a named reverse proxy; verify the
  hop between proxy and game is protected as required.
- PostgreSQL and S2S binding/publication. They must not be reachable from the
  public player network. Confirm access is limited to the named application,
  migration, backup and QA sources.
- Secret mount permissions and ownership; application runtime UID, read-only
  filesystem, dropped capabilities and resource limits; log destinations and
  retention.

Use the read-only host checks in `SECURITY_PUBLIC_BETA.md` as a starting point.
Run them as an authorized administrator because the previous sandbox could
not inspect host firewall state. No firewall or service change is part of
this inventory step.

### Change sequence after approval

1. Approve a written allowlist for the player entry point, administrator
   access, monitoring and any required egress. Confirm PostgreSQL/S2S are
   excluded from public ingress and that no beta/production service is being
   mistaken for QA.
2. Save the current firewall, proxy, service and deployment configuration in
   the owner's protected change system. Define an automatic timed rollback or
   out-of-band console procedure before tightening access.
3. Apply the approved rules in the smallest scope possible. Keep an existing
   administrator session open. Do not open a public port merely to test it.
4. From an authorized external test network, verify the approved player
   endpoint and TLS certificate/protocol; verify PostgreSQL and S2S are
   unreachable externally. Verify denied paths from a separate source where
   available.
5. Inspect the effective service identity, read-only root, capabilities,
   secret file modes, listener bind, published ports, DB role, restart count,
   and health checks on the exact release deployment.

### Rollback

- If the player endpoint or administrator path is broken, restore the saved
  firewall/proxy configuration through the reserved console or timed revert.
  Keep the service private during recovery.
- If PostgreSQL or S2S is publicly reachable, remove the exposure immediately
  using the approved emergency path, preserve sanitized evidence, and review
  access logs before reopening any service.
- If ownership or target identity is uncertain, stop and leave exposure
  closed. Do not test by logging into a possibly unrelated database.

## 3. Designated QA authentication and player permissions

### Establish the two identities

Keep database test access separate from in-game tester access:

| Identity | Purpose | Required boundary |
|---|---|---|
| QA database runner | Runs approved QA fixtures and regression tooling against the designated QA database | Named QA database only; no production access; no superuser/role/database creation. Grant only the fixture/test permissions demonstrably required. Store its secret in the approved protected client configuration. |
| Human playtester | Plays through Python/Godot as an ordinary player | Individual account created through public registration or an approved account workflow; no SysOp or database credentials; no shared player password. |
| QA administrator | Operates the QA environment | Separate named human identity, MFA where supported, short-lived access, and audited elevation. Never use this identity for a gameplay acceptance journey. |

### Verification sequence

1. Environment owner names the tester cohort, account lifecycle owner,
   allowed gameplay permissions, reset policy, report-review contact, and
   support window. Do not create accounts until this is approved.
2. Repair the QA runner's SCRAM configuration in the secret store and
   protected client credential file. Test connectivity to the positively
   identified QA service and database, then confirm database name, server
   identity, role flags and grants. Never probe other listeners or reuse the
   runner credential against an unverified target.
3. Run the canonical database-backed regression with the QA runner. Confirm
   the test report identifies the QA database and exact release image and
   contains no credentials. Check that an intentionally wrong credential is
   rejected without exposing details.
4. Create at least two distinct ordinary player accounts through the same
   Python/Godot login and registration flows intended for testers. Verify
   normal gameplay permissions, account separation, reconnect, logout and
   feedback submission. Exercise SysOp-only commands and confirm ordinary
   accounts are refused.
5. Verify password reset/support, disabling an account, session revocation,
   and test-world reset procedures before admitting the cohort. Rehearse them
   in QA first.

### Rollback

- On failed database authentication, stop the runner and reconcile the
  principal/database/secret with the owner. Do not weaken `pg_hba.conf`, grant
  broad roles or copy credentials from another environment.
- If a tester receives excess privileges, revoke sessions, disable that
  account, inspect the audit trail, and reopen only after the permission
  checks pass.
- If QA data or account state is corrupted, close tester access and restore
  the approved QA snapshot. Do not use the production database as a recovery
  source or target.

## Release gate and evidence package

The owner can approve the following operations individually. External access
stays closed until all applicable gates are signed off:

- [ ] Target environment and system owner positively identified.
- [ ] Exposed/reused credentials rotated; replacement authentication verified;
      old credentials revoked without reactivating a compromised secret.
- [ ] Runtime and migration principals are separate and least-privilege on the
      exact release deployment.
- [ ] Host port ownership, TLS termination, firewall, service identity and
      file permissions reviewed; effective ingress matches the approved
      allowlist.
- [ ] QA runner authenticates only to the intended QA DB; ordinary tester
      accounts have no SysOp or database permissions.
- [ ] Fresh deployment/migration check and agreed regression suite pass on QA.
- [ ] Backup restore, migration recovery, service rollback and incident
      contacts are rehearsed and documented.
- [ ] Feedback outbox can be reviewed by the named operator while GitHub
      publication remains disabled; no public issue is needed for this gate.
- [ ] Change owner explicitly approves the limited playtest window, audience,
      access route, monitoring, stop condition and rollback operator.

Record evidence as redacted commands/results, configuration revision or image
digest, identities/role flags, timestamps and approver. Keep secret values out
of the evidence package. GitHub publication may remain disabled if the feedback
queue is staffed and its retention/backup is covered by the operational plan.
