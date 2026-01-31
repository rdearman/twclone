#ifndef REPO_PLANETS_H
#define REPO_PLANETS_H

#include <jansson.h>
#include <stdbool.h>
#include "db/db_api.h"

/* Q1-Q5: Terra Sanctions */
int db_planets_apply_terra_sanctions(db_t *db, int player_id);

/* Q6: Active Ship Sector */
int db_planets_get_ship_sector(db_t *db, int ship_id, int *sector_id);

/* Q7: Planet Info for Attack */
int db_planets_get_attack_info(db_t *db, int planet_id, int *sector_id, int *owner_id, int *fighters);

/* Q8: Ship Fighters */
int db_planets_get_ship_fighters(db_t *db, int ship_id, int *fighters);

/* Q9: Citadel Defense */
int db_planets_get_citadel_defenses(db_t *db, int planet_id, int *level, int *shields, int *reaction);

/* Q10: Update Citadel Shields */
int db_planets_update_citadel_shields(db_t *db, int planet_id, int new_shields);

/* Q11: Update Ship Fighters */
int db_planets_update_ship_fighters(db_t *db, int ship_id, int loss);

/* Q12: Capture Planet */
int db_planets_capture(db_t *db, int planet_id, int new_owner, const char *new_type);

/* Q13: Defend Planet (Lose Fighters) */
int db_planets_lose_fighters(db_t *db, int planet_id, int loss);

/* Q14: Commodity Illegal Check */
int db_planets_is_commodity_illegal(db_t *db, const char *code, bool *illegal);

/* Q15: Planet Sector Lookup */
int db_planets_get_sector(db_t *db, int planet_id, int *sector_id);

/* Q16: Planet Owner Info */
int db_planets_get_owner_info(db_t *db, int planet_id, int *owner_id, char **owner_type);

/* Q17: Rename Planet */
int db_planets_rename(db_t *db, int planet_id, const char *new_name);

/* Q18: Planet Land Info (same as Q7/Q16) */
int db_planets_get_land_info(db_t *db, int planet_id, int *sector_id, int *owner_id, char **owner_type);

/* Q19: Player ID exists */
int db_planets_check_player_exists(db_t *db, int player_id, bool *exists);

/* Q20: Corp ID exists */
int db_planets_check_corp_exists(db_t *db, int corp_id, bool *exists);

/* Q21: Transfer Ownership */
int db_planets_transfer_ownership(db_t *db, int planet_id, int target_id, const char *target_type);

/* Q22: Citadel Level */
int db_planets_get_citadel_level(db_t *db, int planet_id, int *level);

/* Q23: Add to Treasury */
int db_planets_add_treasury(db_t *db, int planet_id, int amount);

/* Q24: Get Treasury */
int db_planets_get_treasury(db_t *db, int planet_id, long long *balance);

/* Q25: Citadel Level and Treasury */
int db_planets_get_citadel_info(db_t *db, int planet_id, int *level, long long *treasury);

/* Q26: Deduct from Treasury */
int db_planets_deduct_treasury(db_t *db, int planet_id, int amount);

/* Q27: Genesis Idempotency Lookup */
int db_planets_lookup_genesis_idem(db_t *db, const char *key, char **prev_json);

/* Q28: MSL Check */
int db_planets_is_msl_sector(db_t *db, int sector_id, bool *is_msl);

/* Q29: Sector Planet Count */
int db_planets_count_in_sector(db_t *db, int sector_id, int *count);

/* Q30: Ship Genesis Torpedoes */
int db_planets_get_ship_genesis(db_t *db, int ship_id, int *torps);

/* Q31: Planet Type Weights */
int db_planets_get_type_weights(db_t *db, json_t **weights_array);

/* Q32: Planet Type ID Lookup */
int db_planets_get_type_id_by_code(db_t *db, const char *code, int *type_id);

/* Q33: Create Planet */
int db_planets_create(db_t *db, int sector_id, const char *name, int owner_id, const char *owner_type, const char *class_str, int type_id, long long ts, int created_by, int64_t *new_id);

/* Q34: Consume Genesis Torpedo */
int db_planets_consume_genesis(db_t *db, int ship_id);

/* Q35: Update Sector Navhaz */
int db_planets_update_navhaz(db_t *db, int sector_id, int delta);

/* Q36: Genesis Idempotency Insert */
int db_planets_insert_genesis_idem(db_t *db, const char *key, const char *payload, long long ts);

/* Q37: Get Entity Stock */
int db_planets_get_stock(db_t *db, int planet_id, const char *code, int *stock);

/* Q38: Commodity Base Price */
int db_planets_get_commodity_price(db_t *db, const char *code, int *price);

/* Q39: Commodity ID lookup */
int db_planets_get_commodity_id(db_t *db, const char *code, int *id);

/* Q40: (Dup of Q37) */

/* Q41: Add to Treasury (Buy) */
int db_planets_add_treasury_buy(db_t *db, int planet_id, long long amount);

/* Q42: Insert Commodity Order */
int db_planets_insert_buy_order(db_t *db, int player_id, int planet_id, int commodity_id, int qty, int price, int64_t *order_id);

/* Q43: (Dup of Q22) */

/* Q44: Get Fuel Stock */
int db_planets_get_fuel_stock(db_t *db, int planet_id, int *stock);

/* Q45: Update Planet Sector (Transwarp) */
int db_planets_set_sector(db_t *db, int planet_id, int sector_id);

/* Q46: Market Move Stock Info */
int db_planets_get_market_move_info(db_t *db, int planet_id, const char *code, int *current_qty, int *max_ore, int *max_org, int *max_equ);

/* Q47: Upsert Entity Stock */
int db_planets_upsert_stock(db_t *db, int planet_id, const char *code, int quantity);
int db_planets_add_colonists_unassigned(db_t *db, int planet_id, int quantity);
int db_planets_get_colonists_unassigned(db_t *db, int planet_id, int64_t *count);
int db_planets_add_ore_on_hand(db_t *db, int planet_id, int quantity);
int db_planets_add_organics_on_hand(db_t *db, int planet_id, int quantity);
int db_planets_add_equipment_on_hand(db_t *db, int planet_id, int quantity);
int db_planets_get_ore_on_hand(db_t *db, int planet_id, int64_t *count);
int db_planets_get_organics_on_hand(db_t *db, int planet_id, int64_t *count);
int db_planets_get_equipment_on_hand(db_t *db, int planet_id, int64_t *count);

/* Q48: Commodity ID lookup (legacy) */
int db_planets_get_commodity_id_v2(db_t *db, const char *code, int *id);

#endif
