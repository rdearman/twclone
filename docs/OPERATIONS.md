# Operations

This is the operational entry point for local administration, security and deployment readiness. External deployment and cutover procedures are separate from gameplay parity and require their own authorization.

## Release boundary

External beta deployment remains blocked pending an authorized security-group review, production environment and OAuth/session continuity approval, a Cloudflare TLS-mode decision, a final database sync during an approved write freeze, and explicit cutover authorization. The AWS worker remains active; local readiness work does not authorize production changes. Consult the release-candidate audit and deployment hardening plan for current evidence and rollback order.

- [Public beta security baseline](SECURITY_PUBLIC_BETA.md)
- [V3 deployment hardening plan](V3_DEPLOYMENT_HARDENING_PLAN.md)
- [Release-candidate audit](testing/V3_RELEASE_CANDIDATE_AUDIT_20261115.md)
- [SysOp menu contract](SYSOP_MENU.md)
- [Password hashing and authentication](PASSWORD_HASHING.md)
- [Database backend preparation and validation](V3_MYSQL_BACKEND.md)
- [Player feedback system and queue operations](V3_PLAYER_FEEDBACK_SYSTEM.md)
- [Feedback protocol](PROTOCOL.v3/31_Player_Feedback.md)

Do not infer deployment readiness from passing local gameplay suites. The current gameplay release evidence is in [V3 parity](V3_PARITY.md).
