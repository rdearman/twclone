# V3 project status

Status reviewed: 15 November 2026.

## Release position

V3 has substantial gameplay implementation and passed the reported server,
Python and Godot regression gates against disposable PostgreSQL. Selected
ordinary-player journeys, StarPort concurrency/restart cases and feedback
queue behavior were also exercised. These results apply to the operations
listed in the evidence below; they do not certify every feature or deployment.

**External beta deployment is blocked.** The latest release-candidate audit
identifies unresolved credential rotation, host/database exposure ownership,
designated QA authentication, and deployment-specific verification. No
production or beta infrastructure was changed as part of that audit.

## Current evidence

- Overall release and deployment assessment:
  [`testing/V3_RELEASE_CANDIDATE_AUDIT_20261115.md`](testing/V3_RELEASE_CANDIDATE_AUDIT_20261115.md)
- Gameplay implementation and evidence status:
  [`V3_GAMEPLAY_COMPLETION_TRACKER.md`](V3_GAMEPLAY_COMPLETION_TRACKER.md)
- StarPort construction, delivery, restart and rollback acceptance:
  [`testing/V3_STARPORT_ACCEPTANCE_20261114.md`](testing/V3_STARPORT_ACCEPTANCE_20261114.md)
- Godot, concurrency and server restart acceptance:
  [`testing/V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`](testing/V3_GODOT_STARPORT_ACCEPTANCE_20261114.md)
- Security baseline and its environment-specific limitations:
  [`SECURITY_PUBLIC_BETA.md`](SECURITY_PUBLIC_BETA.md)
- Required deployment checks (plan only):
  [`V3_DEPLOYMENT_HARDENING_PLAN.md`](V3_DEPLOYMENT_HARDENING_PLAN.md)

## Sources of truth

The normative protocol is [`PROTOCOL.v3/00_index.md`](PROTOCOL.v3/00_index.md).
Approved gameplay rules and noncanonical defaults are in
[`V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md). The parity overview is
in [`V3_GAMEPLAY_PARITY.md`](V3_GAMEPLAY_PARITY.md). Acceptance reports record
what was actually run; implementation status alone is not verification.

PostgreSQL is the supported full-game runtime. The MySQL driver/API work does
not establish full server runtime support; see
[`V3_MYSQL_BACKEND.md`](V3_MYSQL_BACKEND.md).
