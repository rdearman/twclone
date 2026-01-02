#ifndef DATABASE_CMD_H
#define DATABASE_CMD_H
#include <jansson.h>
#include <stdbool.h>
#include <pthread.h>
#include "db/db_api.h"

typedef struct
{
  long long fee_total;
  long long fee_to_bank;
  long long tax_to_system;
  long long total_fees;
} fee_result_t;

json_t *parse_neighbors_csv (const char *txt);
int db_idemp_store_response (db_t *db,
                             const char *key,
                             const char *response_json);
int db_sector_info_json (db_t *db, int sector_id, json_t **out);
int db_sector_basic_json (db_t *db, int sector_id, json_t **out_obj);
int db_adjacent_sectors_json (db_t *db, int sector_id, json_t **out_array);
int db_ports_at_sector_json (db_t *db, int sector_id, json_t **out_array);
int db_fighters_at_sector_json (db_t *db, int sector_id, json_t **out_array);
int db_mines_at_sector_json (db_t *db, int sector_id, json_t **out_array);
int db_players_at_sector_json (db_t *db, int sector_id, json_t **out_array);
int db_beacons_at_sector_json (db_t *db, int sector_id, json_t **out_array);
int db_planets_at_sector_json (db_t *db, int sector_id, json_t **out_array);

int db_player_set_sector (int player_id, int sector_id);

int db_player_set_alignment (db_t *db, int player_id, int alignment);
int db_player_get_sector (db_t *db, int player_id, int *out_sector);
int db_planet_get_details_json (db_t *db, int pid, json_t **out);
int db_planet_info_json (db_t *db, int player_id, json_t **out);
int db_player_info_selected_fields (db_t *db,
                                    int player_id,
                                    const json_t *fields,
                                    json_t **out);
int db_sector_beacon_text (db_t *db, int sector_id, char **out_text);
int db_ships_at_sector_json (db_t *db,
                             int player_id,
                             int sector_id,
                             json_t **out);
int db_sector_has_beacon (db_t *db, int sector_id);
int db_sector_set_beacon (db_t *db,
                          int sector_id,
                          const char *beacon_text,
                          int player_id);
int db_player_has_beacon_on_ship (db_t *db, int player_id);
int db_player_decrement_beacon_count (db_t *db, int player_id);
int db_ship_flags_set (db_t *db, int ship_id, int mask);
int db_ship_flags_clear (db_t *db, int ship_id, int mask);
int db_ships_inspectable_at_sector_json (db_t *db,
                                         int player_id,
                                         int sector_id,
                                         json_t **out_array);
int db_ship_rename_if_owner (db_t *db,
                             int player_id,
                             int ship_id,
                             const char *new_name);
int db_destroy_ship (db_t *db, int player_id, int ship_id);
int db_create_initial_ship (db_t *db,
                            int player_id,
                            const char *ship_name,
                            int sector_id);
int h_ship_claim_unlocked (db_t *db,
                           int player_id,
                           int sector_id,
                           int ship_id,
                           json_t **out_ship);
int db_ship_claim (db_t *db,
                   int player_id,
                   int sector_id,
                   int ship_id,
                   json_t **out_ship);
int db_ensure_ship_perms_column (db_t *db);
int db_sector_scan_core (db_t *db, int sector_id, json_t **out_obj);
int db_sector_scan_snapshot (db_t *db, int sector_id, json_t **out_core);
int db_notice_create (db_t *db,
                      const char *title,
                      const char *body,
                      const char *severity,
                      time_t expires_at);
json_t *db_notice_list_unseen_for_player (db_t *db, int player_id);
int db_notice_mark_seen (db_t *db, int notice_id, int player_id);
int db_commands_accept (db_t *db,
                        const char *cmd_type,
                        const char *idem_key,
                        json_t *payload,
                        int *out_cmd_id,
                        int *out_duplicate,
                        int *out_due_at);
int db_player_name (db_t *db, int64_t player_id, char **out);
int db_get_ship_name (db_t *db, int ship_id, char **out_name);
int db_get_port_name (db_t *db, int port_id, char **out_name);
int db_chain_traps_and_bridge (db_t *db, int fedspace_max);
int db_path_exists (db_t *db, int from, int to);
int db_rand_npc_shipname (db_t *db, char *out, size_t out_sz);
int db_log_engine_event (long long ts,
                         const char *type,
                         const char *owner_type,
                         int pid,
                         int sid,
                         json_t *payload,
                         db_t *db_opt);
int db_news_insert_feed_item (long long ts,
                              const char *category,
                              const char *scope,
                              const char *headline,
                              const char *body,
                              json_t *context_data);
int db_is_sector_fedspace (db_t *db, int ck_sector);
int db_get_port_id_by_sector (db_t *db, int sector_id);
int db_get_port_sector (db_t *db, int port_id);
int db_get_ship_sector_id (db_t *db, int ship_id);
int db_get_ship_owner_id (db_t *db,
                          int ship_id,
                          int *out_player_id,
                          int *out_corp_id);
bool db_is_ship_piloted (db_t *db, int ship_id);
int db_recall_fighter_asset (db_t *db, int asset_id, int player_id);
int db_get_config_int (db_t *db, const char *key_col_name, int default_value);
bool db_get_config_bool (db_t *db, const char *key_col_name,
                         bool default_value);
int h_add_credits (db_t *db,
                   const char *owner_type,
                   int owner_id,
                   long long amount,
                   const char *tx_type,
                   const char *tx_group_id,
                   long long *new_balance_out);
int h_deduct_credits (db_t *db,
                      const char *owner_type,
                      int owner_id,
                      long long amount,
                      const char *tx_type,
                      const char *tx_group_id,
                      long long *new_balance_out);
int h_get_account_id_unlocked (db_t *db,
                               const char *owner_type,
                               int owner_id,
                               int *account_id_out);
int h_create_bank_account_unlocked (db_t *db,
                                    const char *owner_type,
                                    int owner_id,
                                    long long initial_balance,
                                    int *account_id_out);
int h_get_system_account_id_unlocked (db_t *db,
                                      const char *system_owner_type,
                                      int system_owner_id,
                                      int *account_id_out);
int h_add_credits_unlocked (db_t *db,
                            int account_id,
                            long long amount,
                            const char *tx_type,
                            const char *tx_group_id,
                            long long *new_balance_out);
int h_deduct_credits_unlocked (db_t *db,
                               int account_id,
                               long long amount,
                               const char *tx_type,
                               const char *tx_group_id,
                               long long *new_balance);
long long h_get_config_int_unlocked (db_t *db,
                                     const char *key,
                                     long long default_value);


void h_generate_hex_uuid (char *buffer, size_t buffer_size);
int calculate_fees (db_t *db,
                    const char *tx_type,
                    long long base_amount,
                    const char *owner_type,
                    fee_result_t *out);
int h_update_planet_stock (db_t *db,
                           int planet_id,
                           const char *commodity_code,
                           int quantity_change,
                           int *new_quantity);
int h_update_port_stock (db_t *db,
                         int port_id,
                         const char *commodity_code,
                         int quantity_change,
                         int *new_quantity);
int db_get_player_bank_balance (db_t *db, int player_id,
                                long long *out_balance);
int db_get_corp_bank_balance (db_t *db, int corp_id, long long *out_balance);
int db_get_npc_bank_balance (db_t *db, int npc_id, long long *out_balance);
int db_get_port_bank_balance (db_t *db, int port_id, long long *out_balance);
int db_get_planet_bank_balance (db_t *db, int planet_id,
                                long long *out_balance);
int db_bank_account_exists (db_t *db, const char *owner_type, int owner_id);
int db_bank_create_account (db_t *db,
                            const char *owner_type,
                            int owner_id,
                            long long initial_balance,
                            int *account_id_out);
int db_bank_deposit (db_t *db,
                     const char *owner_type,
                     int owner_id,
                     long long amount);
int db_bank_withdraw (db_t *db,
                      const char *owner_type,
                      int owner_id,
                      long long amount);
int db_bank_transfer (db_t *db,
                      const char *from_owner_type,
                      int from_owner_id,
                      const char *to_owner_type,
                      int to_owner_id,
                      long long amount);
int h_bank_transfer_unlocked (db_t *db,
                              const char *from_owner_type,
                              int from_owner_id,
                              const char *to_owner_type,
                              int to_owner_id,
                              long long amount,
                              const char *tx_type,
                              const char *tx_group_id);
bool h_is_black_market_port (db_t *db, int port_id);
int db_bank_get_transactions (db_t *db,
                              const char *owner_type,
                              int owner_id,
                              int limit,
                              const char *tx_type_filter,
                              long long start_date,
                              long long end_date,
                              long long min_amount,
                              long long max_amount,
                              json_t **out_array);
int db_bank_apply_interest (db_t *db);
int db_bank_process_orders (db_t *db);
int db_bank_set_frozen_status (db_t *db,
                               const char *owner_type,
                               int owner_id,
                               int is_frozen);
int db_bank_get_frozen_status (db_t *db,
                               const char *owner_type,
                               int owner_id,
                               int *out_is_frozen);
int db_commodity_get_price (db_t *db, const char *commodity_code,
                            int *out_price);
int db_commodity_update_price (db_t *db,
                               const char *commodity_code,
                               int new_price);
int db_commodity_create_order (db_t *db,
                               const char *actor_type,
                               int actor_id,
                               const char *commodity_code,
                               const char *side,
                               int quantity,
                               int price);
int db_commodity_fill_order (db_t *db, int order_id, int quantity);
int db_commodity_get_orders (db_t *db,
                             const char *commodity_code,
                             const char *status,
                             json_t **out_array);
int db_commodity_get_trades (db_t *db,
                             const char *commodity_code,
                             int limit,
                             json_t **out_array);
int db_port_get_goods_on_hand (db_t *db,
                               int port_id,
                               const char *commodity_code,
                               int *out_quantity);
int db_port_update_goods_on_hand (db_t *db,
                                  int port_id,
                                  const char *commodity_code,
                                  int quantity_change);
int h_get_port_commodity_quantity (db_t *db,
                                   int port_id,
                                   const char *commodity_code,
                                   int *quantity_out);
int db_planet_get_goods_on_hand (db_t *db,
                                 int planet_id,
                                 const char *commodity_code,
                                 int *out_quantity);
int db_planet_update_goods_on_hand (db_t *db,
                                    int planet_id,
                                    const char *commodity_code,
                                    int quantity_change);
int db_mark_ship_destroyed (db_t *db, int ship_id);
int db_clear_player_active_ship (db_t *db, int player_id);
int db_increment_player_stat (db_t *db, int player_id, const char *stat_name);
int db_get_player_xp (db_t *db, int player_id);
int db_update_player_xp (db_t *db, int player_id, int new_xp);
bool db_shiptype_has_escape_pod (db_t *db, int ship_id);
int db_get_player_podded_count_today (db_t *db, int player_id);
long long db_get_player_podded_last_reset (db_t *db, int player_id);


int db_reset_player_podded_count (db_t *db, int player_id, long long timestamp);
int db_update_player_podded_status (db_t *db,
                                    int player_id,
                                    const char *status,
                                    long long big_sleep_until);
int db_create_podded_status_entry (db_t *db, int player_id);
int db_get_shiptype_info (db_t *db,
                          int shiptype_id,
                          int *holds,
                          int *fighters,
                          int *shields);
int db_player_land_on_planet (db_t *db, int player_id, int planet_id);
int db_player_launch_from_planet (db_t *db, int player_id, int *out_sector_id);
int db_bounty_create (db_t *db,
                      const char *posted_by_type,
                      int posted_by_id,
                      const char *target_type,
                      int target_id,
                      long long reward,
                      const char *description);
int db_player_get_alignment (db_t *db, int player_id, int *alignment);
int db_get_law_config_int (const char *key, int default_value);
char *db_get_law_config_text (const char *key, const char *default_value);
int db_player_get_last_rob_attempt (int player_id,
                                    int *last_port_id_out,
                                    long long *last_attempt_at_out);
int db_player_set_last_rob_attempt (int player_id,
                                    int last_port_id,
                                    long long last_attempt_at);
int db_port_add_bust_record (int port_id,
                             int player_id,
                             long long timestamp,
                             const char *bust_type);
int db_port_is_busted (int port_id, int player_id);
int db_player_update_commission (db_t *db, int player_id);
int h_get_cluster_id_for_sector (db_t *db, int sector_id, int *out_cluster_id);
int h_get_cluster_alignment (db_t *db, int sector_id, int *out_alignment);
int h_get_cluster_alignment_band (db_t *db, int sector_id, int *out_band_id);
int db_commission_for_player (db_t *db,
                              int is_evil_track,
                              long long xp,
                              int *out_commission_id,
                              char **out_title,
                              int *out_is_evil);
int db_alignment_band_for_value (db_t *db,
                                 int align,
                                 int *out_id,
                                 char **out_code,
                                 char **out_name,
                                 int *out_is_good,
                                 int *out_is_evil,
                                 int *out_can_buy_iss,
                                 int *out_can_rob_ports);
int db_ensure_planet_bank_accounts (db_t *db);
int db_get_sector_info (int sector_id,
                        char **out_name,
                        int *out_safe_zone,
                        int *out_port_count,
                        int *out_ship_count,
                        int *out_planet_count,
                        char **out_beacon_text);
int db_news_post (long long ts,
                  long long expiration_ts,
                  const char *category,
                  const char *article_text);
int db_news_get_recent (int player_id, json_t **out_array);
int db_get_online_player_count (void);
int db_get_player_id_by_name (const char *name);
int db_is_black_market_port (db_t *db, int port_id);
int db_get_port_commodity_quantity (db_t *db,
                                    int port_id,
                                    const char *commodity_code,
                                    int *quantity_out);

#endif /* DATABASE_CMD_H */
