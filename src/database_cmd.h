#ifndef DATABASE_CMD_H
#define DATABASE_CMD_H

#include <jansson.h>		/* for json_t */
#include <stdbool.h>
#include <sqlite3.h>
#include <pthread.h>		/* for pthread_mutex_t */
#include <sqlite3.h>




typedef struct
{
  long long fee_total;		// total fee in minor units
  long long fee_to_bank;	// amount credited to bank revenue account
  long long tax_to_system;	// amount credited to system tax account
} fee_result_t;

/* Store final response JSON for a key (after a successful op).
   Returns SQLITE_OK on success. */
json_t *parse_neighbors_csv (const unsigned char *txt);

int db_idemp_store_response (const char *key, const char *response_json);
int db_sector_info_json (int sector_id, json_t ** out);
int db_sector_basic_json (int sector_id, json_t ** out_obj);
int db_adjacent_sectors_json (int sector_id, json_t ** out_array);
int db_ports_at_sector_json (int sector_id, json_t ** out_array);
int db_fighters_at_sector_json (int sector_id, json_t ** out_array);
int db_mines_at_sector_json (int sector_id, json_t ** out_array);
int db_players_at_sector_json (int sector_id, json_t ** out_array);
int db_beacons_at_sector_json (int sector_id, json_t ** out_array);
int db_planets_at_sector_json (int sector_id, json_t ** out_array);
int db_player_set_sector (int player_id, int sector_id);
int db_player_set_alignment (int player_id, int alignment);
int db_player_get_sector (int player_id, int *out_sector);
int db_player_info_json (int player_id, json_t ** out);
int db_player_info_selected_fields(int player_id, const json_t *fields, json_t **out);
int db_sector_beacon_text (int sector_id, char **out_text);	// caller frees *out_text
int db_planets_at_sector_json (int sector_id, json_t ** out_array);
int db_players_at_sector_json (int sector_id, json_t ** out_array);
int db_ports_at_sector_json (int sector_id, json_t ** out_array);
int db_ships_at_sector_json (int player_id, int sector_id, json_t ** out);
int db_sector_has_beacon (int sector_id);
int db_sector_set_beacon (int sector_id, const char *beacon_text,
			  int player_id);
int db_player_has_beacon_on_ship (int player_id);
int db_player_decrement_beacon_count (int player_id);
int db_player_has_beacon_on_ship (int player_id);
int db_player_decrement_beacon_count (int player_id);
int db_get_player_bank_balance (int player_id, long long *out_balance);
int db_ships_inspectable_at_sector_json (int player_id, int sector_id,
					 json_t ** out_array);
int db_ship_claim (int player_id, int sector_id, int ship_id,
		   json_t ** out_ship);
int db_ship_flags_set (int ship_id, int mask);
int db_ship_flags_clear (int ship_id, int mask);
/* List ships in sector (exclude caller’s piloted ship), include ownership & pilot status */
int db_ships_inspectable_at_sector_json (int player_id, int sector_id,
					 json_t ** out_array);
/* Rename if caller owns the ship (via ship_ownership) */
int db_ship_rename_if_owner (int player_id, int ship_id,
			     const char *new_name);
int db_destroy_ship (sqlite3 * db, int player_id, int ship_id);
int db_create_initial_ship (int player_id, const char *ship_name,
			    int sector_id);
/* Claim an unpiloted ship (ownership unchanged); returns JSON of claimed ship */
int db_ship_claim (int player_id, int sector_id, int ship_id,
		   json_t ** out_ship);
int db_ensure_ship_perms_column (void);
int db_sector_scan_core (int sector_id, json_t ** out_obj);
int db_sector_scan_snapshot (int sector_id, json_t ** out_core);


/* Insert a persistent notice; returns row id (>=1) or -1 on error */
int db_notice_create (const char *title, const char *body,
		      const char *severity, time_t expires_at);

/* Return unseen notices for a player as a JSON array (caller owns ref) */
json_t *db_notice_list_unseen_for_player (int player_id);

int db_notice_mark_seen (int notice_id, int player_id);
int db_commands_accept (const char *cmd_type, const char *idem_key,
			json_t * payload, int *out_cmd_id, int *out_duplicate,
			int *out_due_at);
int db_player_name (int64_t player_id, char **out);
int db_get_ship_name (sqlite3 *db, int ship_id, char **out_name);
int db_get_port_name (sqlite3 *db, int port_id, char **out_name);
int db_chain_traps_and_bridge (int fedspace_max /* typically 10 */ );
int db_path_exists (sqlite3 * db, int from, int to);
int db_rand_npc_shipname (char *out, size_t out_sz);
void db_handle_close_and_reset (void);
int db_log_engine_event (long long ts, const char *type,
			 const char *actor_owner_type, int actor_player_id,
			 int sector_id, json_t * payload,
			 const char *idem_key);
int db_news_insert_feed_item (int ts, const char *category, const char *scope,
			      const char *headline, const char *body,
			      json_t * context_data);
int db_is_sector_fedspace (int ck_sector);
int db_get_port_id_by_sector (int sector_id);
int db_get_ship_sector_id (sqlite3 * db, int ship_id);
int db_recall_fighter_asset (int asset_id, int player_id);

// Banking and Stock Management for Market Settlement
// Configuration helpers (thread-safe wrappers)
int db_get_config_int (sqlite3 * db, const char *key_col_name,
		       int default_value);
bool db_get_config_bool (sqlite3 * db, const char *key_col_name,
			 bool default_value);

int h_add_credits (sqlite3 * db, const char *owner_type, int owner_id,
		   long long amount, const char *tx_type,
		   const char *tx_group_id, long long *new_balance_out);
int h_deduct_credits (sqlite3 * db, const char *owner_type, int owner_id,
		      long long amount, const char *tx_type,
		      const char *tx_group_id, long long *new_balance_out);


// Unlocked helper functions (assume db_mutex is already held)
int h_get_account_id_unlocked (sqlite3 * db, const char *owner_type,
			       int owner_id, int *account_id_out);
int h_create_bank_account_unlocked (sqlite3 * db, const char *owner_type,
				    int owner_id, long long initial_balance,
				    int *account_id_out);
int h_get_system_account_id_unlocked (sqlite3 * db,
				      const char *system_owner_type,
				      int system_owner_id,
				      int *account_id_out);
int h_add_credits_unlocked (sqlite3 * db, int account_id, long long amount,
			    const char *tx_type, const char *tx_group_id,
			    long long *new_balance);
int h_deduct_credits_unlocked (sqlite3 * db, int account_id, long long amount,
			       const char *tx_type, const char *tx_group_id,
			       long long *new_balance);
long long h_get_config_int_unlocked (sqlite3 * db, const char *key,
				     long long default_value);
void h_generate_hex_uuid (char *buffer, size_t buffer_size);

// Function to calculate fees for a given transaction type and amount
int calculate_fees (sqlite3 * db, const char *tx_type, long long base_amount,
		    const char *owner_type, fee_result_t * out);
int h_update_planet_stock (sqlite3 * db, int planet_id,
			   const char *commodity_code, int quantity_change,
			   int *new_quantity);
int h_update_port_stock (sqlite3 * db, int port_id,
			 const char *commodity_code, int quantity_change,
			 int *new_quantity);
int db_get_player_bank_balance (int player_id, long long *out_balance);
int db_get_corp_bank_balance (int corp_id, long long *out_balance);
int db_get_npc_bank_balance (int npc_id, long long *out_balance);
int db_get_port_bank_balance (int port_id, long long *out_balance);
int db_get_planet_bank_balance (int planet_id, long long *out_balance);
int db_bank_account_exists (const char *owner_type, int owner_id);
int db_bank_create_account (const char *owner_type, int owner_id,
			    long long initial_balance, int *account_id_out);
int db_bank_deposit (const char *owner_type, int owner_id, long long amount);
int db_bank_withdraw (const char *owner_type, int owner_id, long long amount);
int db_bank_transfer (const char *from_owner_type, int from_owner_id,
		      const char *to_owner_type, int to_owner_id,
		      long long amount);
int h_bank_transfer_unlocked (sqlite3 * db,
                              const char *from_owner_type, int from_owner_id,
                              const char *to_owner_type, int to_owner_id,
                              long long amount,
                              const char *tx_type, const char *tx_group_id);
int db_bank_get_transactions (const char *owner_type, int owner_id, int limit,
			      const char *tx_type_filter, long long start_date, long long end_date,
			      long long min_amount, long long max_amount, json_t ** out_array);
int db_bank_apply_interest ();
int db_bank_process_orders ();
int db_bank_set_frozen_status (const char *owner_type, int owner_id, int is_frozen);
int db_bank_get_frozen_status (const char *owner_type, int owner_id, int *out_is_frozen);
int db_commodity_get_price (const char *commodity_code, int *out_price);
int db_commodity_update_price (const char *commodity_code, int new_price);
int db_commodity_create_order (const char *actor_type, int actor_id,
			       const char *commodity_code, const char *side,
			       int quantity, int price);
int db_commodity_fill_order (int order_id, int quantity);
int db_commodity_get_orders (const char *commodity_code, const char *status,
			     json_t ** out_array);
int db_commodity_get_trades (const char *commodity_code, int limit,
			     json_t ** out_array);
int db_port_get_goods_on_hand (int port_id, const char *commodity_code,
			       int *out_quantity);
int db_port_update_goods_on_hand (int port_id, const char *commodity_code,
				  int quantity_change);
int db_planet_get_goods_on_hand (int planet_id, const char *commodity_code,
				 int *out_quantity);
int db_planet_update_goods_on_hand (int planet_id, const char *commodity_code,
				    int quantity_change);

// New helper functions for ship destruction and player status
int db_mark_ship_destroyed (sqlite3 * db, int ship_id);
int db_clear_player_active_ship (sqlite3 * db, int player_id);
int db_increment_player_stat (sqlite3 * db, int player_id,
			      const char *stat_name);
int db_get_player_xp (sqlite3 * db, int player_id);
int db_update_player_xp (sqlite3 * db, int player_id, int new_xp);
bool db_shiptype_has_escape_pod (sqlite3 * db, int ship_id);
int db_get_player_podded_count_today (sqlite3 * db, int player_id);
long long db_get_player_podded_last_reset (sqlite3 * db, int player_id);
int db_reset_player_podded_count (sqlite3 * db, int player_id,
				  long long timestamp);
int db_update_player_podded_status (sqlite3 * db, int player_id,
				    const char *status,
				    long long big_sleep_until);
int db_create_podded_status_entry (sqlite3 * db, int player_id);
int db_get_shiptype_info (sqlite3 * db, int shiptype_id, int *holds,
			  int *fighters, int *shields);
int db_player_land_on_planet (int player_id, int planet_id);
int db_player_launch_from_planet (int player_id, int *out_sector_id);

int db_bounty_create(sqlite3 *db, const char *posted_by_type, int posted_by_id, const char *target_type, int target_id, long long reward, const char *description);
int db_player_get_alignment(sqlite3 *db, int player_id, int *alignment);

// New Law Enforcement/Robbery Functions
int db_get_law_config_int(const char *key, int default_value);
char *db_get_law_config_text(const char *key, const char *default_value);
int db_player_get_last_rob_attempt(int player_id, int *last_port_id_out, long long *last_attempt_at_out);
int db_player_set_last_rob_attempt(int player_id, int last_port_id, long long last_attempt_at);
int db_port_add_bust_record(int port_id, int player_id, const char *bust_type, long long timestamp);
int db_port_get_active_busts(int port_id, int player_id);
bool h_is_black_market_port(sqlite3 *db, int port_id);




#endif /* DATABASE_CMD_H */
