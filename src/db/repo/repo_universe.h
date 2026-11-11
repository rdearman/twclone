#ifndef REPO_UNIVERSE_H
#define REPO_UNIVERSE_H

#include "db/db_api.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int64_t deal_id;
    int trader_id;
    int player_id;
    int ship_id;
    int corporation_id;
    char trader_code[32];
    char display_name[96];
    char commodity_code[16];
    char side[16];
    char status[16];
    int quantity;
    int64_t unit_price;
    char expires_at[40];
} ferengi_deal_t;

int repo_universe_log_engine_event(db_t *db, const char *type, int sector_id, const char *payload);
db_res_t* repo_universe_get_adjacent_sectors(db_t *db, int sector_id, db_error_t *err);
int repo_universe_get_random_neighbor(db_t *db, int sector_id, int *neighbor_out);
int repo_universe_get_random_unprotected_sector(db_t *db, int *sector_out);
int repo_universe_get_random_port_sector(db_t *db, int *sector_out);
int repo_universe_update_ship_sector(db_t *db, int ship_id, int sector_id);
/* Authoritative player movement: updates the player, active ship, towed
 * ships and towed owners in one nested-safe transaction.  If
 * expected_ship_id is positive, the player's persisted active ship must
 * match it or the operation fails without changing state. */
int repo_universe_move_player_and_ship(db_t *db, int player_id,
                                       int expected_ship_id, int sector_id);
int repo_universe_mass_randomize_zero_sector_ships(db_t *db);
db_res_t* repo_universe_get_orion_ships(db_t *db, int owner_id, db_error_t *err);
int repo_universe_ensure_orion_population(db_t *db, int corporation_id,
                                           int home_sector);
int repo_universe_update_ship_target(db_t *db, int ship_id, int target_sector);
int repo_universe_get_corp_owner_by_tag(db_t *db, const char *tag, int *owner_id_out);
int repo_universe_get_port_sector_by_id_name(db_t *db, int port_id, const char *name, int *sector_out);
int repo_universe_get_cluster_center_sector_by_role(db_t *db, const char *role, int *sector_out);
db_res_t* repo_universe_search_index(db_t *db, const char *q, int limit, int offset, int search_type, db_error_t *err);
db_res_t* repo_universe_get_density_sector_list(db_t *db, int target_sector, db_error_t *err);
int repo_universe_get_sector_density(db_t *db, int sector_id, int *density_out);
int repo_universe_warp_exists(db_t *db, int from, int to, int *exists_out);
db_res_t* repo_universe_get_interdictors(db_t *db, int sector_id, db_error_t *err);
int repo_universe_sector_has_port(db_t *db, int sector_id, int *has_port_out);
int repo_universe_get_max_sector_id(db_t *db, int *max_id_out);
int repo_universe_get_warp_count(db_t *db, int *count_out);
db_res_t* repo_universe_get_all_warps(db_t *db, db_error_t *err);
/* Return directed exits whose source sector is current or visited by player. */
db_res_t* repo_universe_get_player_known_warps(db_t *db, int player_id,
                                                int current_sector_id,
                                                db_error_t *err);
db_res_t* repo_universe_get_asset_counts(db_t *db, int sector_id, db_error_t *err);
int repo_universe_set_beacon(db_t *db, int sector_id, const char *text);
int repo_universe_check_transwarp(db_t *db, int ship_id, int *enabled_out);
/* Requires an active transaction; locks an existing destination fighter row
 * so the homing beacon cannot be removed before transwarp settlement. */
int repo_universe_lock_sector_fighter(db_t *db, int sector_id,
                                      int *found_out);
int repo_universe_update_player_sector(db_t *db, int player_id, int sector_id);
int repo_universe_get_ferengi_corp_info(db_t *db, int *corp_id_out, int *player_id_out);
int repo_universe_ensure_ferengi_traders(db_t *db, int corporation_id, int home_sector);
db_res_t *repo_universe_get_all_ferengi_traders(db_t *db, db_error_t *err);
int repo_universe_get_ferengi_trader_by_ship(db_t *db, int ship_id,
                                             int *trader_id_out,
                                             int *corporation_id_out);
int repo_universe_get_ferengi_captain(db_t *db, int *trader_id_out,
                                      int *ship_id_out);
int repo_universe_ship_is_orion_owned(db_t *db, int ship_id,
                                      int *is_orion_out);
db_res_t *repo_universe_get_players_in_sector(db_t *db, int sector_id, db_error_t *err);
db_res_t *repo_universe_get_ferengi_traders_at_sector(db_t *db, int sector_id, int player_id, db_error_t *err);
db_res_t *repo_universe_get_ferengi_trader_deals(db_t *db, int player_id, db_error_t *err);
int repo_universe_get_ferengi_deal(db_t *db, int64_t deal_id, int for_update, ferengi_deal_t *out);
int repo_universe_create_ferengi_offer(db_t *db, int trader_id, int player_id, int visit_number, int ship_id, int corporation_id, int64_t now_s, int64_t *deal_id_out);
int repo_universe_record_ferengi_interaction(db_t *db, const ferengi_deal_t *deal, const char *type, int reputation_delta, const char *idempotency_key);
int repo_universe_transition_ferengi_deal(db_t *db, int64_t deal_id, const char *status, int64_t now_s, int *changed_out);
int repo_universe_expire_ferengi_deals(db_t *db, int64_t now_s);
int repo_universe_advance_ferengi_trader(db_t *db, int trader_id, int ship_id, int sector_id);
int repo_universe_get_ferengi_homeworld_sector(db_t *db, int *sector_out);
int repo_universe_get_ferengi_warship_type_id(db_t *db, int *type_id_out);
int repo_universe_get_random_wormhole_neighbor(db_t *db, int sector_id, int *neighbor_out);
int repo_universe_get_candidate_homeworld_sectors(db_t *db, int **sectors_out, int *count_out);
int repo_universe_ensure_ferengi_homeworld(db_t *db, int sector_id, int owner_id);
int repo_universe_relocate_orion_base(db_t *db, int sector_id, int port_id, int planet_id, int owner_id);

/* Shared NPC action/idempotency and intelligence persistence. */
int repo_npc_action_claim(db_t *db, const char *idempotency_key,
                          const char *faction, const char *action_type,
                          int npc_ship_id, int target_player_id,
                          int sector_id, const char *payload);
int repo_npc_action_complete(db_t *db, const char *idempotency_key,
                             const char *status, const char *payload);
int repo_npc_intel_upsert(db_t *db, int npc_ship_id, int target_player_id,
                          int sector_id, const char *source,
                          int64_t expires_at);
int repo_npc_intel_get_sector(db_t *db, int npc_ship_id, int *player_id_out,
                              int *sector_id_out);
int repo_npc_intel_get_sector_source(db_t *db, int npc_ship_id,
                                     int *player_id_out, int *sector_id_out,
                                     char *source_out, size_t source_size);
int repo_npc_security_incident_claim(db_t *db, const char *idempotency_key,
                                     int offender_player_id,
                                     const char *asset_kind, int asset_id,
                                     int sector_id, int64_t expires_at);

#endif // REPO_UNIVERSE_H
