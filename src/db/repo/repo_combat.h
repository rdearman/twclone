#ifndef REPO_COMBAT_H
#define REPO_COMBAT_H

#include <jansson.h>
#include <stdbool.h>
#include "db/db_api.h"

/* Structure to hold basic ship combat stats */
typedef struct {
    int id;
    int hull;
    int fighters;
    int shields;
} repo_combat_ship_t;

/* Structure for asset queries */
typedef struct {
    int id;
    int quantity;
    int owner_id;
    int corp_id;
    int mode;
} repo_combat_asset_t;

/* MSL */
bool db_combat_is_msl_sector(db_t *db, int sector_id);

/* Fighters on Entry */
int db_combat_get_ship_stats(db_t *db, int ship_id, repo_combat_ship_t *out);
int db_combat_update_ship_stats(db_t *db, int ship_id, int hull, int fighters, int shields);
int db_combat_get_hostile_fighters(db_t *db, int sector_id, int **out_ids, int **out_quantities, int **out_owners, int **out_corps, int **out_modes, int *out_count);
int db_combat_delete_sector_asset(db_t *db, int asset_id);

/* Flee */
int db_combat_get_flee_info(db_t *db, int ship_id, int *maxholds, int *sector_id);
int db_combat_get_first_adjacent_sector(db_t *db, int sector_id, int *dest_sector);
int db_combat_move_ship_and_player(db_t *db, int ship_id, int player_id, int dest_sector);

/* Deploy Assets List */
int db_combat_list_fighters(db_t *db, int player_id, json_t **out_array);
int db_combat_list_mines(db_t *db, int player_id, json_t **out_array);

/* Deploy Fighters */
int db_combat_get_ship_sector_locked(db_t *db, int ship_id, int *sector_id);
int db_combat_lock_sector(db_t *db, int sector_id);
int db_combat_sum_sector_fighters(db_t *db, int sector_id, int *total);
int db_combat_consume_ship_fighters(db_t *db, int ship_id, int amount);
int db_combat_insert_sector_fighters(db_t *db, int sector_id, int owner_id, int corp_id, int mode, int amount);
int db_combat_get_max_asset_id(db_t *db, int sector_id, int owner_id, int asset_type, int64_t *out_id);

/* Deploy Mines */
int db_combat_sum_sector_mines(db_t *db, int sector_id, int *total);
int db_combat_consume_ship_mines(db_t *db, int ship_id, int asset_type, int amount);
int db_combat_insert_sector_mines(db_t *db, int sector_id, int owner_id, int corp_id, int asset_type, int mode, int amount);

/* Load Ship Combat Stats (Locked/Unlocked) */
typedef struct {
    int id;
    int hull;
    int shields;
    int fighters;
    int sector;
    char name[64];
    int attack_power;
    int defense_power;
    int max_attack;
    int player_id;
    int corp_id;
} repo_combat_ship_full_t;

int db_combat_load_ship_full_locked(db_t *db, int ship_id, repo_combat_ship_full_t *out, bool skip_locked);
int db_combat_load_ship_full_unlocked(db_t *db, int ship_id, repo_combat_ship_full_t *out);
int db_combat_persist_ship_damage(db_t *db, int ship_id, int hull, int shields, int fighters_lost);

/* Armid/Limpet/Quasar Helpers */
int db_combat_select_armid_mines_locked(db_t *db, int sector_id, json_t **out_array);
int db_combat_select_limpets_locked(db_t *db, int sector_id, json_t **out_array);
int db_combat_check_limpet_attached(db_t *db, int ship_id, int owner_id, bool *attached);
int db_combat_decrement_or_delete_asset(db_t *db, int asset_id, int quantity);
int db_combat_attach_limpet(db_t *db, int ship_id, int owner_id, int64_t created_ts);
int db_combat_get_planet_quasar_info(db_t *db, int sector_id, json_t **out_array);
int db_combat_get_planet_atmosphere_quasar(db_t *db, int planet_id, int *owner_id, char *owner_type_buf, int *base_str, int *reaction);

/* Mines Recall */
int db_combat_get_ship_mine_capacity(db_t *db, int ship_id, int *sector_id, int *current, int *max);
int db_combat_get_asset_info(db_t *db, int asset_id, int owner_id, int sector_id, int *quantity, int *type);
int db_combat_recall_mines(db_t *db, int asset_id, int ship_id, int quantity);

/* Handle Sector Entry Hazards (Toll) */
int db_combat_get_sector_fighters_locked(db_t *db, int sector_id, json_t **out_array);

/* New additions for missing functions */
int db_combat_get_ship_fighter_capacity(db_t *db, int ship_id, int *sector_id, int *current, int *max, int *corp_id);
int db_combat_get_asset_info_locked(db_t *db, int asset_id, int sector_id, int asset_type, int *quantity, int *owner_id, int *corp_id, int *mode);
int db_combat_add_ship_fighters(db_t *db, int ship_id, int amount);
int db_combat_select_mines_locked(db_t *db, int sector_id, int asset_type, json_t **out_array);
int db_combat_debit_credits(db_t *db, int player_id, int amount);
int db_combat_update_asset_quantity(db_t *db, int asset_id, int new_quantity);
int db_combat_get_stardock_locations(db_t *db, int **out_sectors, int *out_count);

#endif
