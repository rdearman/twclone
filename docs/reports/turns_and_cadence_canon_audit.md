# Turns and Cadence Canon Audit Report

**Date:** 2026-01-31
**Subject:** TradeWars 2002 Canon Parity Audit — Turns, Daily Cadence, and Time-based Mechanics

## 1. Turn Model Summary

Turns in `twclone` are managed as a finite resource stored in a dedicated database table.

- **Storage**: The `turns` table (`player_id`, `turns_remaining`, `last_update`) is the source of truth.
- **Consumption**: Managed via the `h_consume_player_turn` helper. Most state-changing actions (movement, trading, combat) cost exactly **1 turn**. Scans and information commands are currently turn-free.
- **Regeneration**: A daily cron task (`daily_turn_reset`) resets all players' `turns_remaining` to the global `turnsperday` configuration value.

---

## 2. Canon Checklist Table

| Item | Requirement | Status | Evidence |
| :--- | :--- | :--- | :--- |
| 1 | **Finite Resource** | ✅ Implemented | Stored in `turns` table; decremented via `repo_players_consume_turns`. |
| 2 | **Action-dependent Cost** | ⚠️ Partial | Most actions cost 1 turn. Scans/Info are 0. No multi-turn actions found. |
| 3 | **No Negative Turns** | ✅ Implemented | `repo_players_consume_turns` uses `WHERE turns_remaining >= {1}`. |
| 4 | **Daily Regeneration** | ✅ Implemented | Scheduled at `daily@03:00Z` in `092_seed_gameplay.sql`. |
| 5 | **Daily Maintenance** | ✅ Implemented | Extensive daily tasks (bank interest, market settlement, etc.) in `src/server_cron.c`. |
| 6 | **Server-authoritative Time** | ✅ Implemented | Regeneration uses `sql_now_timestamptz(db)` and server system time. |
| 7 | **No Per-request Regen** | ✅ Implemented | Regeneration is isolated to the `daily_turn_reset` cron task. |
| 8 | **Configurable Limits** | ✅ Implemented | `turnsperday` key in `config` table; used by cron and auth registration. |
| 9 | **No Turn Farming** | ✅ Implemented | Cron locks and `UPDATE` semantics prevent farming/duplicate regen. |
| 10 | **No Time Skew Exploits** | ✅ Implemented | Task locks in `cron_tasks` table track `last_run` to prevent double-regen. |

---

## 3. Cadence Analysis

The server defines "Daily" via a centralized scheduler that executes tasks at specific UTC times.

- **Turn Reset**: Occurs at `03:00Z`.
- **Market Settlement**: Occurs at `05:30Z`.
- **Bank Interest**: Occurs at `00:00Z`.
- **News Compilation**: Occurs at `06:00Z`.

This distributed cadence ensures that maintenance is spread out but consistently follows a 24-hour cycle. This aligns with canon expectations of a "daily maintenance" period, although the specific "midnight" varies by task.

---

## 4. Exploit Analysis

- **Accumulation Bypass**: The current implementation of `db_cron_reset_daily_turns` performs a hard reset: `SET turns_remaining = config.turnsperday`. Players cannot "bank" turns from previous days. While this is one interpretation of canon, it prevents turn accumulation which some canon variants allow.
- **Transaction Safety**: `h_consume_player_turn` is called within the command handlers, usually *before* the primary action. This ensures that a player cannot perform an action if they have 0 turns.
- **Information Leaks**: Information-gathering commands (scans, port info) are currently free. This could allow infinite "free" scanning loops, which is canonically acceptable but may differ from "hard" turn-limit environments.

---

## 5. Test Gaps

1. **Turn Exhaustion Test**: `test_turn_exhaustion_refusal`
   - Assert that an action (e.g., `move.warp`) returns `1489 Insufficient turns` when `turns_remaining` is 0.
2. **Deterministic Regeneration Test**: `test_daily_regen_deterministic`
   - Assert that running the `daily_turn_reset` task twice in the same cycle does not result in double turns or errors.
3. **Turn Persistence Test**: `test_turn_persistence_across_reconnect`
   - Assert that `turns_remaining` remains correct after a logout/login cycle.

---

## 6. Key Code Pointers

- **Turn Helper**: `h_consume_player_turn` (`src/server_players.c:1443`)
- **Repo Consumer**: `repo_players_consume_turns` (`src/db/repo/repo_players.c:341`)
- **Regen Logic**: `h_daily_turn_reset` (`src/server_cron.c:787`)
- **DB Reset**: `db_cron_reset_daily_turns` (`src/db/repo/repo_cron.c:851`)
- **Schedule Seed**: `sql/pg/092_seed_gameplay.sql:552`
