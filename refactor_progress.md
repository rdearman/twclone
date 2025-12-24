# Database Abstraction Layer Refactor - Progress Report

**Objective:** Abstract the database layer to support multiple backends (PostgreSQL and SQLite) using a generic `db_t` API, with strict transaction boundaries, row-level locking, and transient error retries.

## 1. Infrastructure & Build System
*   **Generic DB API:** `src/db/db_api.h` and `src/db/db_api.c`.
*   **Nesting Support:** `db_api.c` handles nested transaction calls using a `tx_nest_level` counter.
*   **SQLite Compatibility:** `db_sqlite.c` now strips `FOR UPDATE` to support portable row-locking syntax.
*   **Logic Manager:** `src/game_db.h/c` manages the global `db_t`.
*   **Error Model:** Sequential `ERR_DB_*` codes in `src/errors.h`.

## 2. Fully Refactored (Generic API + TX/Retry/Row Lock Pattern)
The following have been migrated and conform to the strict concurrency and transaction requirements:

### `src/server_loop.c` & `src/server_engine.c`
*   Entire files migrated (Main loops, cron runner, executors).

### `src/database_cmd.c`
*   All core player/bank helpers (`h_get_config_int_unlocked`, `h_add_credits_unlocked`, `h_get_account_id_unlocked`, etc.).
*   `db_player_update_commission` (TX/Retry/Row Lock added).

### `src/server_players.c`
*   `h_player_apply_progress` (TX/Retry/Row Lock added).

### `src/server_planets.c`
*   Refactored `cmd_planet_info`, `cmd_planet_rename`, `cmd_planet_transfer_ownership`, `cmd_planet_deposit`, `cmd_planet_withdraw`, `cmd_planet_genesis_create`, `cmd_planet_market_sell`, `cmd_planet_market_buy_order`, `cmd_planet_transwarp`, `cmd_planet_colonists_set`, `cmd_planet_colonists_get`.
*   Refactored helpers: `h_is_illegal_commodity`, `h_planet_check_trade_legality`, `h_get_planet_owner_info`, `h_market_move_planet_stock`, `h_get_entity_stock_quantity`, `cmd_planet_land`, `cmd_planet_launch`.

### `src/server_players.c` (In Progress)
*   Refactored `h_get_player_petty_cash`, `h_decloak_ship`.

### `src/server_universe.c`
*   Entire file migrated (Warping, sectors, NPC logic, scanning).
*   Refactored Ferengi and ISS patrol logic to use `db_t`.

### `src/server_ships.c`
*   Entire file migrated (Ship management, cargo, towing, destruction).
*   Added `ship_update_cargo` stored procedure in `sql/pg/200_gameplay_procs.sql`.

### `src/database_cmd.c` (In Progress)
*   Refactored ship-related helpers: `db_ships_inspectable_at_sector_json`, `db_ship_rename_if_owner`, `db_ship_claim`, `h_ship_claim_unlocked`, `db_player_info_json`, `db_is_sector_fedspace`, `db_get_ship_sector_id`, `db_get_ship_owner_id`, `db_is_ship_piloted`, `db_mark_ship_destroyed`, `db_clear_player_active_ship`, `db_increment_player_stat`, `db_get_player_xp`, `db_update_player_xp`, `db_shiptype_has_escape_pod`, `db_get_player_podded_count_today`, `db_get_player_podded_last_reset`, `db_reset_player_podded_count`, `db_update_player_podded_status`, `db_create_podded_status_entry`.

### `src/server_combat.c` (In Progress)
*   **Completed Commands:** `cmd_combat_attack`, `handle_ship_attack`, `cmd_combat_status`, `cmd_combat_deploy_fighters`, `cmd_fighters_recall`, `cmd_combat_deploy_mines`, `cmd_combat_lay_mines`, `cmd_mines_recall`, `cmd_combat_scrub_mines`, `handle_combat_flee`, `cmd_combat_attack_planet`, `cmd_deploy_fighters_list`, `cmd_deploy_mines_list`, `cmd_combat_sweep_mines`.
*   **Completed Helpers:** `sum_sector_fighters`, `sum_sector_mines`, `insert_sector_mines`, `insert_sector_fighters`, `get_sector_mine_counts`, `h_apply_quasar_damage`, `apply_sector_quasar_on_entry`, `h_trigger_atmosphere_quasar`, `ship_consume_fighters`, `ship_consume_mines`, `db_get_stardock_sectors`, `h_apply_terra_sanctions`, `apply_armid_mines_on_entry`, `apply_limpet_mines_on_entry`, `apply_sector_fighters_on_entry`, `load_ship_combat_stats`, `persist_ship_damage`.

### `src/server_ports.c` (In Progress)
*   **Completed Commands:** `cmd_trade_port_info`, `cmd_trade_buy`, `cmd_trade_sell`.
*   **Completed Helpers:** `h_calculate_port_sell_price`, `h_calculate_port_buy_price`, `h_update_port_stock`, `h_update_entity_stock`, `h_get_entity_stock_quantity`, `h_get_port_commodity_details`, `h_port_buys_commodity`, `commodity_to_code`.

## 3. Current State & Next Tasks
*   **Current Stop Point:** Finished major trading commands (`buy`/`sell`) in `src/server_ports.c`.
*   **Immediate Next Tasks:**
    1.  Finish refactoring remaining commands in `src/server_ports.c` (jettison, robbery, etc.).
    2.  Refactor `src/server_citadel.c`.
    3.  Continue refactoring remaining helpers in `src/database_cmd.c` and `src/server_players.c`.