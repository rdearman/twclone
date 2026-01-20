# 29. SysOp Commands (v1)

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
*   **Game Rules**: `turnsperday`, `neutral_band`, `illegal_allowed_neutral`, `max_cloak_duration`, `limpet_ttl_days`
*   **Economy**: `bank_min_balance_for_interest`, `bank_max_daily_interest_per_account`, `planet_treasury_interest_rate_bps`, `bank_alert_threshold_player`, `bank_alert_threshold_corp`
*   **Shipyard**: `shipyard_enabled`, `shipyard_trade_in_factor_bp`, `shipyard_require_cargo_fit`, `shipyard_require_fighters_fit`, `shipyard_require_shields_fit`, `shipyard_require_hardware_compat`, `shipyard_tax_bp`

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
      { "key": "shipyard_tax_bp", "value": "500", "type": "int", "desc": "Shipyard tax in basis points" }
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
