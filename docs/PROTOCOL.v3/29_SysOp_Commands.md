# 29. SysOp Commands (v1)

Canonical gameplay/configuration decisions: [`../V3_GAMEPLAY_DECISIONS.md`](../V3_GAMEPLAY_DECISIONS.md).

## Overview

This document defines the **SysOp (System Operator)** RPC commands.
These commands are strictly for **Live Operations** (tuning, moderation, recovery).
They are NOT for database administration, universe generation, or unrestricted "god mode" actions.

**Roles & Security**
*   All commands require specific roles (`sysop`, `gm`, or `observer`).
*   Unauthorized calls return `1407 Forbidden`.
*   All mutations (writes) are **Audited** in the `engine_audit` table.

## 1. SysOp Config (Live Tuning)

Allows tuning of specific server parameters without a restart.
**Scope**: Strictly limited to the **Allowed Key List**. All others are `1407 Refused`.

### Allowed Keys (v1)
*   **Game Rules**: `turnsperday`, `neutral_band`, `illegal_allowed_neutral`, `illegal_min_experience`, `max_cloak_duration`, `limpet_ttl_days`
*   **Economy**: `bank_min_balance_for_interest`, `bank_max_daily_interest_per_account`, `planet_treasury_interest_rate_bps`, `bank_alert_threshold_player`, `bank_alert_threshold_corp`, `planet_market.buy_order_expiry_seconds`
*   **Shipyard**: `shipyard_enabled`, `shipyard_trade_in_factor_bp`, `shipyard_sale_factor_bp`, `shipyard_require_cargo_fit`, `shipyard_require_fighters_fit`, `shipyard_require_shields_fit`, `shipyard_require_hardware_compat`, `shipyard_tax_bp`
*   **Planet population**: `planet.slave_unrest_interval_days`, `planet.slave_unrest_stoppage_cycles`, `planet.slave_unrest_population_loss`

### Planet slave unrest policy

The engine policy fields are `planet.slave_unrest_interval_days` (days),
`planet.slave_unrest_stoppage_cycles` (production cycles), and
`planet.slave_unrest_population_loss` (fixed number of population units). Each
accepts a nonnegative integer. `0` is an explicit unset/disabled sentinel, not
a gameplay default. No active gameplay interval or loss value is selected.
Unrest processing remains inactive unless all three are configured positive.
Even with values present, production/loss activation remains blocked until the
deterministic rule for allocating losses across unassigned and working
population is approved. No scheduler currently consumes these settings.

### `sysop.config.list`
List all allowed config keys and their current values.

**Role**: `sysop`
**Request**: `{}`
**Response**:
```json
{
  "type": "sysop.config.list_v1",
  "data": {
    "items": [
      { "key": "turnsperday", "value": "1000", "type": "int", "desc": "Turns allocated per day" },
      { "key": "shipyard_tax_bp", "value": "500", "type": "int", "desc": "Shipyard tax in basis points" },
      { "key": "planet_market.buy_order_expiry_seconds", "value": "86400", "type": "int", "desc": "Planet buy-order lifetime in seconds (60-604800)" }
    ]
  }
}
```

### `sysop.config.get`
Get a specific config value.

**Role**: `sysop`
**Args**: `{ "key": "turnsperday" }`
**Response**: `{ "key": "turnsperday", "value": "1000", "type": "int" }`
**Errors**:
*   `1407 Refused`: Key is not in the allowed list.

### `sysop.config.set`
Update a config value.

**Role**: `sysop`
**Args**:
*   `key`: String (Must be in allow-list).
*   `value`: String (Parsed server-side based on type).
*   `confirm`: Boolean (Must be `true`).
*   `note`: String (Optional audit note).

**Request**:
```json
{
  "command": "sysop.config.set",
  "data": {
    "key": "turnsperday",
    "value": "1200",
    "confirm": true,
    "note": "Bonus weekend"
  }
}
```
**Response**: `sysop.config.ack_v1` `{ "key": "turnsperday", "old_value": "1000", "new_value": "1200" }`

**Errors**:
*   `1407 Refused`: Key not allowed, type mismatch, or bounds violation.
*   `1302 Invalid Argument`: Missing confirmation.

`planet_market.buy_order_expiry_seconds` is an integer from 60 through 604800
inclusive. Its default is 86400 seconds when unset. Changing it affects newly
placed planet buy orders only; orders already open retain their persisted
`expires_at` timestamp.

`shipyard_sale_factor_bp` is an integer from 0 through 10000 inclusive, default
6000. It sets the standalone ship-sale percentage in basis points and does not
change `shipyard_trade_in_factor_bp` used by ship upgrades.

### `sysop.config.history`
View audit history for a key.

**Role**: `sysop`
**Args**: `{ "key": "turnsperday", "limit": 10 }`
**Response**: `sysop.config.history_v1`
```json
{
  "items": [
    { "ts": "2025-10-20T10:00:00Z", "actor_id": 1, "old_value": "1000", "new_value": "1200", "note": "Bonus" }
  ]
}
```

## 2. Player Operations

Operational actions for managing players. No "god mode" editing of assets.

### `sysop.players.search`
Search for players.

**Role**: `gm`, `sysop`
**Args**: `{ "q": "name_or_partial", "limit": 20 }`
**Response**: `sysop.players_v1`
```json
{
  "players": [
    { "id": 101, "name": "TraderJoe", "last_login": "2025-10-19T10:00:00Z", "status": "active" }
  ]
}
```

### `sysop.player.get`
Get basic operational details (ID, name, corp, status).

**Role**: `gm`, `sysop`
**Args**: `{ "player_id": 101 }`
**Response**: `sysop.player_v1`

### `sysop.player.kick`
Force disconnect a player (and invalidate session).

**Role**: `gm`, `sysop`
**Args**: `{ "player_id": 101, "reason": "Policy violation", "confirm": true }`
**Response**: `sysop.player.ack_v1` `{ "kicked": true }`

### `sysop.player.sessions.get`
View active connection details for a player (IP, session start, client info).

**Role**: `sysop`
**Args**: `{ "player_id": 101 }`
**Response**: `sysop.player.sessions_v1`
```json
{
  "sessions": [
    { "id": "sess_abc", "ip": "192.168.1.5", "connected_at": "...", "client": "TW-Term/2.0" }
  ]
}
```

## 3. Messaging

Manage system-wide communications.

### `sysop.notice.create`
Create a `system.notice` (sticky news/alert).

**Role**: `gm`, `sysop`
**Args**:
*   `title`: String
*   `body`: String
*   `severity`: "info" | "warn" | "error"
*   `expires_at`: ISO-8601 Timestamp

**Response**: `sysop.notice.ack_v1` `{ "notice_id": 500 }`

### `sysop.notice.delete`
Remove a notice early.

**Role**: `gm`, `sysop`
**Args**: `{ "notice_id": 500 }`
**Response**: `sysop.notice.ack_v1` `{ "deleted": true }`

### `sysop.broadcast.send`
Send an immediate ephemeral broadcast to all online players.

**Role**: `gm`, `sysop`
**Args**:
*   `message`: String
*   `severity`: "info" | "warn" | "error"

**Response**: `sysop.broadcast.ack_v1` `{ "delivered": 42 }`

## 4. Engine & Jobs

Diagnose and manage the asynchronous job queue.

### `sysop.engine_status.get`
Get high-level engine health (S2S link status, heartbeats).

**Role**: `observer`, `gm`, `sysop`
**Request**: `{}`
**Response**: `sysop.engine_status_v1`
```json
{
  "s2s_connected": true,
  "last_heartbeat": "2025-10-20T12:00:00Z",
  "queue_depth": 5
}
```

### `sysop.jobs.list`
List jobs in the queue.

**Role**: `sysop`
**Args**: `{ "status": "pending|failed|deadletter", "limit": 50 }`
**Response**: `sysop.jobs_v1` `{ "jobs": [...] }`

### `sysop.jobs.get`
Get details of a specific job.

**Role**: `sysop`
**Args**: `{ "job_id": 999 }`
**Response**: `sysop.job_v1`

### `sysop.jobs.retry`
Retry a failed/deadletter job.

**Role**: `sysop`
**Args**: `{ "job_id": 999 }`
**Response**: `sysop.jobs.ack_v1` `{ "retried": true }`

### `sysop.jobs.cancel`
Cancel a pending/failed job.

**Role**: `sysop`
**Args**: `{ "job_id": 999 }`
**Response**: `sysop.jobs.ack_v1` `{ "cancelled": true }`
