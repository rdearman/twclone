# 08. Engine Event Contract

**Scope**: Defines the **durable database contracts** (tables and schemas) used for asynchronous communication between Server and Engine.

**Principles**:
*   **Source of Truth**: The Database (`events`, `commands` tables).
*   **Atomicity**: Server inserts events in the same transaction as the player mutation.
*   **Idempotency**: Commands and events use `idem_key`.

## 1. Server-Emitted Events (`engine_events` Table)

Server inserts these when player actions occur. Engine consumes them.

**Schema**: `events(id, ts, type, actor_player_id, sector_id, payload, idem_key)`

### Event Types
*   **`player.move.v1`**: `{ player_id, old_sector, new_sector, fuel_used }`
*   **`player.dock.v1`**: `{ player_id, sector_id, port_id }`
*   **`player.trade.v1`**: `{ player_id, port_id, cargo_id, quantity, credits_change, type }`
*   **`player.mine.v1`**: `{ player_id, sector_id, cargo_id, quantity }`
*   **`player.corp_join.v1`**: `{ player_id, corp_id }`
*   **`player.planet_transfer.v1`**: `{ player_id, planet_id, cargo_id, quantity, type }`
*   **`player.planet_attack.v1`**: `{ player_id, planet_id, damage_dealt, capture_attempt }`
*   **`player.port_strike.v1`**: `{ player_id, port_id, damage_dealt }`
*   **`player.illegal_act.v1`**: `{ player_id, sector_id, type, target_id }`
*   **`npc.destroy.v1`**: `{ npc_id, destroyed_by, sector_id }`
*   **`combat.ship_damage.v1`**: `{ target_ship_id, is_player, damage, source_id }`

## 2. Engine-Enqueued Commands (`engine_commands` Table)

Engine inserts these to mutate the world. Server consumes and executes them.

**Schema**: `commands(id, type, payload, status, priority, due_at, idem_key)`

### Command Types

**Economy & Environment**
*   **`port.refresh_stock_and_price.v1`**: `{ port_id, resource_adjustments[] }`
*   **`bank.pay_interest.v1`**: `{ interest_rate, type }`
*   **`planet.resource_growth.v1`**: `{ planet_id, cargo_adjustments[], fighter_adjustment }`
*   **`sector.cleanse_mines_fighters.v1`**: `{ sector_id, type, removal_count }`
*   **`economy.update.v1`**: `{ port_id, type, factor, cargo_id }`

**NPC & Gameplay**
*   **`npc.spawn.v1`**: `{ type, sector_id, cargo, initial_credits, max_turns }`
*   **`npc.move.v1`**: `{ npc_id, target_sector }`
*   **`npc.attack.v1`**: `{ npc_id, target_player_id }`
*   **`npc.deploy_fighters.v1`**: `{ npc_id, sector_id, count, lifespan_ms }`

**Player & Corp State**
*   **`player.apply_decay.v1`**: `{ player_id, turns_granted, status_cleared[] }`
*   **`player.adjust_turns.v1`**: `{ player_id, amount, reason }`
*   **`player.adjust_credits.v1`**: `{ player_id, amount, reason }`
*   **`player.adjust_cargo.v1`**: `{ player_id, cargo_id, quantity }`
*   **`player.destroy_ship.v1`**: `{ player_id, reason }`
*   **`player.set_captured.v1`**: `{ player_id, is_captured, captured_by_id }`
*   **`player.damage_equipment.v1`**: `{ player_id, equipment_slot, damage_amount }`
*   **`player.update_status.v1`**: `{ player_id, status_type, expires_at }`
*   **`corp.adjust_funds.v1`**: `{ corp_id, amount, reason }`
*   **`corp.destroy.v1`**: `{ corp_id, reason }`

**System**
*   **`server_broadcast.v1`**: `{ title, body, severity, expires_at, target_player_id }`
*   **`admin.config.set.v1`**: `{ key, value, expires_at }`

## 3. Notices (`system_notice` Table)

Engine inserts notices for broadcast.
**Schema**: `system_notice(id, ts, scope, sector_id, corp_id, player_id, message, meta, ephemeral, ttl_seconds)`

## 4. Zones & Scheduling

*   **Zones**: `zones`, `sector_zones` tables define FedSpace, MSL.
*   **Cron Tasks**: Engine internal scheduler for periodic jobs (`daily_turn_reset`, `terra_replenish`).
