# Alignment Consequences Canon Audit Report

**Date:** 2026-01-31
**Subject:** TradeWars 2002 Canon Parity Audit — Alignment Mechanics

## 1. Alignment Model Summary

Alignment in `twclone` is a numeric state stored per player that represents their moral standing in the galaxy.

- **Storage**: Stored as an `integer` in the `players.alignment` column.
- **Range**: Hard-capped between `-2000` and `2000`.
- **Thresholds**: Defined in the `alignment_band` table. Key bands include:
  - `MONSTROUS`: -2000 to -1001
  - `VERY_EVIL`: -1000 to -501
  - `SHADY`: -500 to -250
  - `NEUTRAL`: -249 to 249
  - `GOOD`: 250 to 749
  - `VERY_GOOD`: 750 to 2000
- **Mutation**: Alignment is modified via `h_player_apply_progress`, which updates both alignment and experience points (XP).

---

## 2. Canon Checklist Table

| Item | Requirement | Status | Evidence |
| :--- | :--- | :--- | :--- |
| 1 | **Numeric & Persistent** | ✅ Implemented | Stored in `players` table; persists across sessions. |
| 2 | **Thresholds/Bands** | ✅ Implemented | `alignment_band` table defines 6 distinct ranks. |
| 3 | **Trading Effects** | ✅ Implemented | `player.trade.v1` events trigger penalties for illegal goods. |
| 4 | **Combat Effects** | ⚠️ Partial | `port.rob` impacts alignment. `handle_ship_destruction` currently only affects XP. |
| 5 | **Crime Interaction** | ✅ Implemented | `port.rob.bust` increases alignment (ironically, as a "forgiveness" or "reset" mechanism). |
| 6 | **Illegal Markets** | ✅ Implemented | `h_can_trade_commodity` gates visibility/trading of illegal goods server-side. |
| 7 | **FedSpace/ISS** | ✅ Implemented | `h_fedspace_cleanup` tows players with `alignment < 0` out of sectors 1-10. |
| 8 | **Earth Restrictions** | ✅ Implemented | `cmd_planet_land` refuses landing on Earth (S1) if `alignment < 0`. |
| 9 | **Corp Constraints** | ❌ Missing | No alignment-based corp eligibility rules found in `server_corporation.c`. |
| 10 | **Anti-farming** | ✅ Implemented | Alignment changes tied to state-mutating events; planet conversions have no alignment deltas. |

---

## 3. Alignment Mutation Map

| Trigger | Delta | Implementation |
| :--- | :--- | :--- |
| **Illegal Trade** | Variable (Negative) | `h_compute_illegal_alignment_delta` in `src/server_engine.c`. Scaled by player band and cluster band. |
| **Port Robbery (Success)** | -10 to -20 | `cmd_port_rob` in `src/server_ports.c`. |
| **Port Robbery (Bust)** | +15 | `cmd_port_rob` in `src/server_ports.c`. (Caught criminals "pay" via alignment reset). |
| **Buying a Round** | +5 (Configurable) | `cmd_tavern_round_buy` in `src/server_stardock.c`. |
| **New Registration** | +1 | `cmd_auth_register` in `src/server_auth.c`. |

---

## 4. Consequence Map

| Effect | Check | Code Pointer |
| :--- | :--- | :--- |
| **Illegal Goods Visibility** | `alignment > neutral_band` (Hide) | `cmd_trade_port_info` in `src/server_ports.c` |
| **Illegal Goods Trading** | `alignment > neutral_band` (Refuse) | `h_can_trade_commodity` in `src/server_ports.c` |
| **FedSpace Protection** | `alignment < 0` (Towed) | `h_fedspace_cleanup` in `src/server_cron.c` |
| **Earth Landing** | `alignment < 0` (Refused) | `cmd_planet_land` in `src/server_planets.c` |
| **Slave/Colonist Conversion** | `alignment < 0` (Convert) | `cmd_planet_deposit` / `withdraw` in `src/server_planets.c` |
| **Underground Password** | `alignment >= 0` (Refused) | `cmd_tavern_trader_buy_password` in `src/server_stardock.c` |

---

## 5. Exploit Analysis

- **Planet Conversion Farming**: The investigation confirmed that Slave ↔ Colonist conversion in `server_planets.c` does **not** apply alignment deltas. This prevents a loop where a player could infinitely farm alignment by moving units between ship and planet.
- **Idempotency**: Trade-based deltas are processed via the engine's event-sourced progress handler. Since trades use `idempotency_key` and events are processed in order, the risk of double-application is minimized at the architectural level.
- **Hard Caps**: `h_player_apply_progress` enforces hard boundaries at `-2000` and `2000`, preventing integer overflow or underflow exploits.

---

## 6. Test Gaps

1. **Alignment Decay/Reset Test**: `test_alignment_normalization_over_time`
   - Verify if alignment naturally drifts toward zero (canonical in some versions). Currently, it appears static until acted upon.
2. **Aggression Penalty Test**: `test_alignment_hit_on_attack`
   - Assert that attacking a good-aligned player results in an alignment hit. (Currently noted as a `TODO` in `server_engine.c`).
3. **Bounty Interaction Test**: `test_alignment_impact_on_bounty_eligibility`
   - Assert that Federation bounties can only be posted on evil players by good players.

---

## 7. Key Code Pointers

- **Progress Resolver**: `h_player_apply_progress` (`src/server_players.c:1561`)
- **Engine Progress**: `h_player_progress_from_event_payload` (`src/server_engine.c:454`)
- **Penalty Logic**: `h_compute_illegal_alignment_delta` (`src/server_engine.c:390`)
- **Trading Gating**: `h_can_trade_commodity` (`src/server_ports.c:505`)
- **FedSpace Cleanup**: `h_fedspace_cleanup` (`src/server_cron.c:558`)
- **Earth Block**: `cmd_planet_land` (`src/server_planets.c:652`)
