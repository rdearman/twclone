# SysOp Menu (v1) — Specification

**Version:** 1.0 (Live Operations)
**Scope:** Strict Operational Limits. No DB Admin. No World Gen.

## 1. Overview

The SysOp Menu allows operators (`sysop`) and Game Masters (`gm`) to manage a running server.
It uses the standard JSON protocol (see `PROTOCOL.v3/29_SysOp_Commands.md`).

**Key Principles:**
1.  **Safety**: No direct SQL access. Restricted config keys.
2.  **Audit**: All mutations are recorded in `engine_audit`.
3.  **Roles**: strict RBAC (`observer`, `gm`, `sysop`).

## 2. Allowed Configuration Keys

Only the following keys are exposed via `sysop.config.*` commands.
**All other keys are immutable** via this interface.

*   `turnsperday`
*   `neutral_band`
*   `illegal_allowed_neutral`
*   `max_cloak_duration`
*   `limpet_ttl_days`
*   `bank_min_balance_for_interest`
*   `bank_max_daily_interest_per_account`
*   `planet_treasury_interest_rate_bps`
*   `bank_alert_threshold_player`
*   `bank_alert_threshold_corp`
*   `shipyard_enabled`
*   `shipyard_trade_in_factor_bp`
*   `shipyard_require_cargo_fit`
*   `shipyard_require_fighters_fit`
*   `shipyard_require_shields_fit`
*   `shipyard_require_hardware_compat`
*   `shipyard_tax_bp`

## 3. Command Allow-List

### A. Configuration (Role: `sysop`)
*   `sysop.config.list`
*   `sysop.config.get`
*   `sysop.config.set`
*   `sysop.config.history`

### B. Player Operations (Role: `gm`, `sysop`)
*   `sysop.players.search`
*   `sysop.player.get`
*   `sysop.player.kick`
*   `sysop.player.sessions.get`

### C. Messaging (Role: `gm`, `sysop`)
*   `sysop.notice.create`
*   `sysop.notice.delete`
*   `sysop.broadcast.send`

### D. Engine & Jobs (Role: `sysop` / Read-Only `observer`)
*   `sysop.engine_status.get` (Read-only)
*   `sysop.jobs.list`
*   `sysop.jobs.get`
*   `sysop.jobs.retry`
*   `sysop.jobs.cancel`

## 4. User Interface Structure

The SysOp client (terminal or web) should organize these commands into:

1.  **Config**: Table view of allowed keys. Edit dialog with strict type validation.
2.  **Players**: Search bar. Player detail view (Status + Sessions). Kick button.
3.  **Messaging**: Active Notices list. "New Notice" form. "Send Broadcast" form.
4.  **Engine**: Health status indicators. Job queue list (filterable by status). Retry/Cancel actions.

## 5. Error Handling

*   **1407 Refused**: User lacks role or attempted to access forbidden config key.
*   **1302 Invalid Argument**: Malformed value or missing confirmation.
*   **1503 Database Error**: Internal failure.

## 6. Audit Logging

Every successful mutation command (`set`, `kick`, `create`, `retry`, `cancel`) must write to `engine_audit`.
Entries include:
*   `actor_id`
*   `command`
*   `payload` (sanitized)
*   `timestamp`