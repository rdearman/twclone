# Intra-Stack Protocol: Server ↔ Game Engine (S2S)

**Status:** Finalised **v1.5** (Guaranteed Complete)
**Scope:** Defines message exchange between the **player-facing Server** and the **simulation-focused Game Engine**. Transport is **durable DB rails** plus a **TCP control link**.

> Canon notes: “Stardock” (hardware shop, shipyard, etc.), class-9 port behaviour, and special ports (Sol/Rylos/Alpha Centauri) are part of TW2002 canon. Use these to shape shops, bank, equipment, and police/fedspace rules. ([tw-attac.com][2])

---

## 1) Roles & Boundaries

| Process    | Primary responsibilities                                                | Data flow (input)            | Data flow (output)           |
| ---------- | ----------------------------------------------------------------------- | ---------------------------- | ---------------------------- |
| **Server** | Sessions/Auth, RPC execution, validation, fast reads, client broadcasts | Consumes **engine_commands** | Emits **engine_events**      |
| **Engine** | Cron & ticks, economy/growth, NPC loop, sanctions, world mutations      | Consumes **engine_events**   | Enqueues **engine_commands** |

---

## 2) Communication Channels

All communication uses **two durable tables** and **one TCP control channel**.

| Channel             | Direction       | Guarantees                                | Primary use                     |
| ------------------- | --------------- | ----------------------------------------- | ------------------------------- |
| **engine_events**   | Server → Engine | Guaranteed delivery, ordered by `(ts,id)` | Player actions (facts)          |
| **engine_commands** | Engine → Server | Exactly-once (via `idempotency_key`)      | World state mutations           |
| **TCP S2S**         | Bi-directional  | Low-latency, non-durable                  | Health, config pushes, shutdown |

---

## 3) Message Envelope (common)

All messages conform to:

```json
{ "v": 1, "id": "uuid", "ts": 1730000000, "src": "server|engine", "dst": "engine|server",
  "type": "string", "payload": { } }
```

Appendix A defines **per-type payload schemas**.

---

## 4) Server-Emitted Events → `engine_events`

> Inserted **in the same transaction as the validated player mutation** (see §7).

* `player.move.v1` — `{ player_id, old_sector, new_sector, fuel_used }`
* `player.dock.v1` — `{ player_id, sector_id, port_id }`
* `player.trade.v1` — `{ player_id, port_id, cargo_id, quantity, credits_change, type }`
* `player.mine.v1` — `{ player_id, sector_id, cargo_id, quantity }`
* `player.corp_join.v1` — `{ player_id, corp_id }`
* `player.planet_transfer.v1` — `{ player_id, planet_id, cargo_id, quantity, type }`
* `player.planet_attack.v1` — `{ player_id, planet_id, damage_dealt, capture_attempt }`
* `player.port_strike.v1` — `{ player_id, port_id, damage_dealt }`
* `player.illegal_act.v1` — `{ player_id, sector_id, type, target_id }`
* `npc.destroy.v1` — `{ npc_id, destroyed_by, sector_id }`
* `combat.ship_damage.v1` — `{ target_ship_id, is_player, damage, source_id }`

---

## 5) Engine-Enqueued Commands → `engine_commands`

### Admin

* `admin.config.set.v1` — `{ key, value, expires_at }`
* `admin.engine.force_tick.v1` — `{ job_type, scope, scope_id }`
* `admin.player.update_all_ships.v1` — `{ update_field, new_value, reason }`

### Cron & Economy

* `port.refresh_stock_and_price.v1` — `{ port_id, resource_adjustments[] }`
* `bank.pay_interest.v1` — `{ interest_rate, type }`
* `planet.resource_growth.v1` — `{ planet_id, cargo_adjustments[], fighter_adjustment }`
* `sector.cleanse_mines_fighters.v1` — `{ sector_id, type, removal_count }`
* `player.apply_decay.v1` — `{ player_id, turns_granted, status_cleared[] }`
* `economy.update.v1` — `{ port_id, type, factor, cargo_id }`

### NPC & Gameplay

* `npc.spawn.v1` — `{ type, sector_id, cargo, initial_credits, max_turns }`
* `npc.move.v1` — `{ npc_id, target_sector }`
* `npc.attack.v1` — `{ npc_id, target_player_id }`
* `npc.deploy_fighters.v1` — `{ npc_id, sector_id, count, lifespan_ms }`
* `ship.transfer.v1` — `{ ship_id, is_player_ship, target_sector }`
* `player.adjust_turns.v1` — `{ player_id, amount, reason }`
* `player.destroy_ship.v1` — `{ player_id, reason }`
* `player.adjust_credits.v1` — `{ player_id, amount, reason }`
* `player.adjust_cargo.v1` — `{ player_id, cargo_id, quantity }`
* `player.set_captured.v1` — `{ player_id, is_captured, captured_by_id }`
* `player.damage_equipment.v1` — `{ player_id, equipment_slot, damage_amount }`
* `corp.adjust_funds.v1` — `{ corp_id, amount, reason }`
* `corp.destroy.v1` — `{ corp_id, reason }`
* `server_broadcast.v1` — `{ title, body, severity, expires_at, target_player_id }`
* `player.update_status.v1` — `{ player_id, status_type, expires_at }`
* `world.set_sector_status.v1` — `{ sector_id, status }`

---

## 6) TCP Control Messages

* `s2s.health.check` (↔) — `{ tick_lag_ms }`
* `s2s.engine.pause` (Server→Engine) — `{ reason }`
* `s2s.engine.shutdown` (Server→Engine) — `{ reason }`
* `s2s.config.refresh` (Server→Engine) — `{ version }`
* `s2s.error` (Any→Any) — `{ code, message, details }`

---

## 7) **Implementation Guidelines (CRUCIAL)**

1. **Atomicity & Order**
   Server **must** insert `engine_events` **within the same DB transaction** as the player mutation (e.g., dock, trade) so the Engine consumes a totally ordered, loss-free event stream.

2. **Single Consumer**
   Engine acquires an **advisory lock** (or leader-election) so only **one** engine instance processes durable rails at a time.

3. **Exactly-Once Commands**
   Server treats `engine_commands` as **immutable jobs**. Each has an `idempotency_key`. The Server must:

   * Begin Tx → `SELECT FOR UPDATE` on `idempotency_key` index
   * If unseen: mark consumed, run mutation, commit; if seen: **no-op**.

4. **Back-pressure & Retry**
   Engine uses exponential backoff on transient DB errors; commands/events include a small `attempts` counter for ops visibility.

5. **Logging**

   * TCP link: log all messages at **debug**.
   * Durable rails: warn/error on validation, idempotency, or execution failures.

6. **Sector-scoped Broadcasts**
   Server helper `broadcast_to_sector(sector_id, event)` for notices like docking, movement, combat start; “global” allowed behind a feature flag.

7. **TTL & Cleanup**
   `system_notice` rows carry a TTL; daily (and 5-min) sweep jobs delete expired rows in bounded batches.

> Canon expectations for sectors/ports/shops/banks/shipyards reflect TW2002: class-9 ports containing Stardock services, special fighter/shield/hold ports, etc. Use these to seed shop lists and bank mechanics. ([tw-attac.com][2])

---

## 8) Player ↔ Server RPC (client-visible loop)

These are **client RPCs** (outside S2S) that often **emit events** which the Engine consumes:

* `nav.move` → validates adjacency/path; **emits** `player.move.v1`.
* `port.dock` → server-side checks; **emits** `player.dock.v1`; also inserts a `system.notice` (“docked”) to sector feed.
* `trade.buy|sell` → resolves port inventory/price; **emits** `player.trade.v1`.
* `ship.status` (and alias `ship.info`) → fast read; no event.
* `bank.balance.get` (Stardock-gated) → fast read; no event.
* `hardware.list` (Stardock & special ports) → fast read; no event.
*   `hardware.buy` → purchase hardware; no event.
* `corp.join|leave` → **emits** `player.corp_join.v1`.
* `planet.deposit|withdraw` → **emits** `player.planet_transfer.v1`.
* `combat.attack.port|planet|ship` → **emits** `player.port_strike.v1` or `combat.ship_damage.v1`.
* `police.bribe|surrender` (fedspace policing) → **emits** `player.illegal_act.v1` (resolution type).
* `scan.sector` / `scan.long` → reads & caches; no event.

> Special ports & services (e.g., Sol/Rylos/Alpha Centauri selling fighters/shields/holds) guide `hardware.list` content and price curves. ([breakintochat.com][3])

---

## 9) SysOp Menu → Admin RPC → Engine/Server mapping

SysOp actions are **admin RPCs** that typically enqueue **engine_commands** (or call server maintenance). This preserves auditability and idempotency.

| SysOp menu item                            | Admin RPC (Server)                                                                 | Effect on rails                                                             |            |                                                 |
| ------------------------------------------ | ---------------------------------------------------------------------------------- | --------------------------------------------------------------------------- | ---------- | ----------------------------------------------- |
| **BigBang / Re-seed universe**             | `admin.engine.force_tick.v1` with `job_type=port_refresh` and a maintenance script | Populates ports/sectors/lanes; posts summary `server_broadcast.v1`          |            |                                                 |
| **Grant turns to player**                  | `player.adjust_turns.v1`                                                           | Engine command → server consumes and updates                                |            |                                                 |
| **Adjust player credits**                  | `player.adjust_credits.v1`                                                         | Engine command                                                              |            |                                                 |
| **Spawn NPC in sector**                    | `npc.spawn.v1`                                                                     | Engine command                                                              |            |                                                 |
| **Force economy tick**                     | `admin.engine.force_tick.v1` (`planet_growth                                       | decay                                                                       | interest`) | Engine consumes, enqueues specific job commands |
| **Set sector status (Fed/Neutral/Closed)** | `world.set_sector_status.v1`                                                       | Engine command                                                              |            |                                                 |
| **Destroy player ship**                    | `player.destroy_ship.v1`                                                           | Engine command                                                              |            |                                                 |
| **Corp funds adjust / destroy**            | `corp.adjust_funds.v1` / `corp.destroy.v1`                                         | Engine commands                                                             |            |                                                 |
| **Global notice**                          | `server_broadcast.v1`                                                              | Engine→Server to unify audit trail (or direct server tool with same schema) |            |                                                 |
| **Config set (game flags, rates)**         | `admin.config.set.v1`                                                              | Engine command + `s2s.config.refresh`                                       |            |                                                 |
| **Pause / Shutdown Engine**                | (none)                                                                             | TCP: `s2s.engine.pause` / `s2s.engine.shutdown`                             |            |                                                 |

---

## 10) Canon Tasks & Feature Notes (for parity)

Use these to shape your world rules and manifests; the references below are descriptive, not binding text.

* **Stardock services**: shipyards, hardware emporium, etc.; often located with a class-9 port. Drives `hardware.list`, `bank.balance`, `shipyard.*`. ([tw-attac.com][2])
* **Special ports**: **Sol** (fighters), **Rylos** (shields), **Alpha Centauri** (cargo holds) — handy for early progression menus & pricing. ([breakintochat.com][3])
* **Core ship stats**: holds, shields, fighters, scanners; visible in `ship.status`; purchasable/upgradeable via shops. ([wiki.classictw.com][4])
* **Sector display & adjacency**: sector info includes beacons/mines/fighters/planets/adjacent sectors; your `nav.*` RPCs should mirror this. ([GameBanshee][5])
* **Daily maintenance**: interest payments, stock/price refresh, decay/cleanup ticks are standard sysop jobs. ([wiki.classictw.com][4])

---

## 11) Localisation Handshake (hello v1.5)

* Client may include `{ ui_locale, catalog?{ name, version, hash } }` in `client.hello`.
* Server replies with `{ schema: "1.5", ui_locale, required_catalog_hash }` and prints these in the banner.
* An `i18n.catalog_manifest.get` RPC returns `{ schema, keys[], hash }` for client verification.

(These are client RPCs, not S2S; listed here for completeness because they affect envelopes shown in broadcasts.)

---

## 12) Broadcast Envelope (client-facing)

**Envelope:** `{ type, data, ts }` (no `reply_to`).
**Client rules:** at-most-once render; ignore unknown `type`; sector scoping preferred; TTL honoured by sweeper.

Example (sector notice on docking):

```json
{ "type": "system.notice", "ts": 1730001111,
  "data": { "code": "port.docked.v1", "player_id": 42, "sector_id": 99, "port_id": 9001 } }
```

---

## 13) Operational Cadence

* **Broadcast pump:** ~**500 ms** sweep; seen-markers provide idempotency.
* **TTL sweeps:** quick (5-min) + daily cleanup for `system_notice`.
* **Engine tick:** 5–10 s loop with backoff; cron jobs schedule economy/interest/decay.

---

## Appendix A — **Full JSON Schemas** (copy/paste canonical)

```json
{
  "event_schemas": {
    "player.move.v1": { "player_id": "int", "old_sector": "int", "new_sector": "int", "fuel_used": "float" },
    "player.dock.v1": { "player_id": "int", "sector_id": "int", "port_id": "int" },
    "player.trade.v1": { "player_id": "int", "port_id": "int", "cargo_id": "int", "quantity": "int", "credits_change": "int", "type": "buy|sell" },
    "player.mine.v1": { "player_id": "int", "sector_id": "int", "cargo_id": "int", "quantity": "int" },
    "player.corp_join.v1": { "player_id": "int", "corp_id": "int" },
    "player.planet_transfer.v1": { "player_id": "int", "planet_id": "int", "cargo_id": "int", "quantity": "int", "type": "deposit|withdraw" },
    "player.planet_attack.v1": { "player_id": "int", "planet_id": "int", "damage_dealt": "int", "capture_attempt": "bool" },
    "player.port_strike.v1": { "player_id": "int", "port_id": "int", "damage_dealt": "int" },
    "player.illegal_act.v1": { "player_id": "int", "sector_id": "int", "type": "bust|mine_illegal|attack_unarmed", "target_id": "int|null" },
    "npc.destroy.v1": { "npc_id": "int", "destroyed_by": "int|null", "sector_id": "int" },
    "combat.ship_damage.v1": { "target_ship_id": "int", "is_player": "bool", "damage": "int", "source_id": "int" }
  },
  "command_schemas": {
    "admin.config.set.v1": { "key": "string", "value": "string|int|float", "expires_at": "int|null" },
    "admin.engine.force_tick.v1": { "job_type": "port_refresh|planet_growth|decay|interest", "scope": "global|sector|player", "scope_id": "int|null" },
    "admin.player.update_all_ships.v1": { "update_field": "turns|fuel|shields|hold_capacity", "new_value": "int", "reason": "string" },
    "port.refresh_stock_and_price.v1": { "port_id": "int", "resource_adjustments": "array<{cargo_id: int, quantity: int, price_factor: float}>" },
    "bank.pay_interest.v1": { "interest_rate": "float", "type": "player|corp" },
    "planet.resource_growth.v1": { "planet_id": "int", "cargo_adjustments": "array<{cargo_id: int, quantity: int}>", "fighter_adjustment": "int" },
    "sector.cleanse_mines_fighters.v1": { "sector_id": "int|null", "type": "mines|fighters|beacons", "removal_count": "int|null" },
    "player.apply_decay.v1": { "player_id": "int", "turns_granted": "int", "status_cleared": "array<string>" },
    "economy.update.v1": { "port_id": "int", "type": "growth|decay|tax", "factor": "float", "cargo_id": "int|null" },
    "npc.spawn.v1": { "type": "string", "sector_id": "int", "cargo": "object", "initial_credits": "int", "max_turns": "int" },
    "npc.move.v1": { "npc_id": "int", "target_sector": "int" },
    "npc.attack.v1": { "npc_id": "int", "target_player_id": "int" },
    "npc.deploy_fighters.v1": { "npc_id": "int|null", "sector_id": "int", "count": "int", "lifespan_ms": "int" },
    "ship.transfer.v1": { "ship_id": "int", "is_player_ship": "bool", "target_sector": "int" },
    "player.adjust_turns.v1": { "player_id": "int", "amount": "int", "reason": "string" },
    "player.destroy_ship.v1": { "player_id": "int", "reason": "string" },
    "player.adjust_credits.v1": { "player_id": "int", "amount": "int", "reason": "string" },
    "player.adjust_cargo.v1": { "player_id": "int", "cargo_id": "int", "quantity": "int" },
    "player.set_captured.v1": { "player_id": "int", "is_captured": "bool", "captured_by_id": "int|null" },
    "player.damage_equipment.v1": { "player_id": "int", "equipment_slot": "string", "damage_amount": "int" },
    "corp.adjust_funds.v1": { "corp_id": "int", "amount": "int", "reason": "string" },
    "corp.destroy.v1": { "corp_id": "int", "reason": "string" },
    "server_broadcast.v1": { "title": "string", "body": "string", "severity": "info|warn|error", "expires_at": "int|null", "target_player_id": "int|null" },
    "player.update_status.v1": { "player_id": "int", "status_type": "wanted|clean|flee", "expires_at": "int|null" },
    "world.set_sector_status.v1": { "sector_id": "int", "status": "fedspace|neutral|port_closed" }
  },
  "tcp_control_schemas": {
    "s2s.health.check": { "tick_lag_ms": "int|null" },
    "s2s.engine.pause": { "reason": "string" },
    "s2s.engine.shutdown": { "reason": "string" },
    "s2s.config.refresh": { "version": "int" },
    "s2s.error": { "code": "unsupported_type|bad_envelope|toolarge", "message": "string", "details": "object|null" }
  }
}
```

---

## Appendix B — Canon-aligned content map (for your docs)

If you want to include canon pointers (not required by protocol, but useful for designers/testers), you can add a short “Lore & Mechanics” page that lists:

* **Stardock & class-9 port services** (hardware, shipyard, bank). ([tw-attac.com][2])
* **Special purchase ports** (fighters/shields/holds). ([breakintochat.com][3])
* **Typical daily maintenance jobs** (interest, stock/price refresh, decay). ([wiki.classictw.com][4])
* **Sector display elements & adjacency** for UI parity (mines, beacons, fighters, planets). ([GameBanshee][5])
* **Further reading** (v1/v2 docs, sysop notes, helper tooling) for your team: ([wiki.classictw.com][1])

---

### Ready to drop into your repo

[1]: https://wiki.classictw.com/index.php/TradeWars_2002_v1_Documentation_Text?utm_source=chatgpt.com "TradeWars 2002 v1 Documentation Text"
[2]: https://www.tw-attac.com/docs/TradeWars.html?utm_source=chatgpt.com "Trade Wars 2002 Version 3 - attac"
[3]: https://breakintochat.com/wiki/TradeWars_2002?utm_source=chatgpt.com "TradeWars 2002 - Break Into Chat - BBS wiki"
[4]: https://wiki.classictw.com/index.php/TradeWars_2002_v2_%28HVS%29_Documentation_Text?utm_source=chatgpt.com "TradeWars 2002 v2 (HVS) Documentation Text"
[5]: https://www.gamebanshee.com/bbs/guides/tradewars2002.php?utm_source=chatgpt.com "Trade Wars 2002 - GameBanshee BBS"
