#ifndef SCHEMAS_H
#define SCHEMAS_H
#include <jansson.h>

/**
 * @brief Schema generator function type.
 */
typedef json_t *(*schema_generator_fn) (void);

/**
 * @brief Returns a "Not Implemented" schema placeholder.
 */
json_t *schema_placeholder (void);

/**
 * @brief Build the system.capabilities payload.
 */
json_t *capabilities_build (void);

/**
 * @brief (C2S) Return a JSON Schema object for a client-facing key.
 *
 * @param key The schema key (e.g., "auth.login", "move.warp").
 * @return A new json_t* reference to the schema, or NULL if not found.
 */
json_t *schema_get (const char *key);

/**
 * @brief Shutdown the schema system and free the cache.
 */
void schema_shutdown (void);

/**
 * @brief (C2S) Return an array of all available client-facing schema keys.
 *
 * @return A new json_t* reference to an array of strings.
 */
json_t *schema_keys (void);

/**
 * @brief (C2S) Validate a client payload against its registered JSON Schema.
 *
 * @param type The command key (e.g., "auth.login").
 * @param payload The JSON object payload from the client.
 * @param why A pointer to a char* that will be set to an error message on failure.
 * @return 0 on success, -1 on failure.
 */
int schema_validate_payload (const char *type, json_t * payload, char **why);

/**
 * @brief (S2S) Manually validate an inter-server (s2s) payload.
 *
 * @param type The S2S command key (e.g., "s2s.health.check").
 * @param payload The JSON object payload from the other server.
 * @param why A pointer to a char* that will be set to an error message on failure.
 * @return 0 on success, -1 on failure.
 */
int s2s_validate_payload (const char *type, json_t * payload, char **why);

/* --- Registry Hooks (Implemented in server_loop.c) --- */
json_t *loop_get_schema_for_command (const char *name);
json_t *loop_get_all_schema_keys (void);

/* --- Schema Generators (Exposed for Registry) --- */
json_t *schema_envelope (void);
json_t *schema_auth_login (void);
json_t *schema_auth_register (void);
json_t *schema_auth_logout (void);
json_t *schema_auth_refresh (void);
json_t *schema_auth_mfa_totp_verify (void);
json_t *schema_admin_notice (void);
json_t *schema_admin_shutdown_warning (void);
json_t *schema_system_capabilities (void);
json_t *schema_system_describe_schema (void);
json_t *schema_system_hello (void);
json_t *schema_system_disconnect (void);
json_t *schema_session_ping (void);
json_t *schema_session_hello (void);
json_t *schema_session_disconnect (void);
json_t *schema_ship_inspect (void);
json_t *schema_ship_rename (void);
json_t *schema_ship_reregister (void);
json_t *schema_ship_claim (void);
json_t *schema_ship_status (void);
json_t *schema_ship_info (void);
json_t *schema_ship_transfer_cargo (void);
json_t *schema_ship_jettison (void);
json_t *schema_ship_upgrade (void);
json_t *schema_ship_repair (void);
json_t *schema_ship_self_destruct (void);
json_t *schema_hardware_list (void);
json_t *schema_hardware_buy (void);
json_t *schema_port_info (void);
json_t *schema_port_status (void);
json_t *schema_port_describe (void);
json_t *schema_port_rob (void);
json_t *schema_trade_port_info (void);
json_t *schema_trade_buy (void);
json_t *schema_trade_sell (void);
json_t *schema_trade_quote (void);
json_t *schema_trade_jettison (void);
json_t *schema_trade_offer (void);
json_t *schema_trade_accept (void);
json_t *schema_trade_cancel (void);
json_t *schema_trade_history (void);
json_t *schema_move_describe_sector (void);
json_t *schema_move_scan (void);
json_t *schema_move_warp (void);
json_t *schema_move_pathfind (void);
json_t *schema_move_autopilot_start (void);
json_t *schema_move_autopilot_stop (void);
json_t *schema_move_autopilot_status (void);
json_t *schema_sector_info (void);
json_t *schema_sector_search (void);
json_t *schema_sector_set_beacon (void);
json_t *schema_sector_scan_density (void);
json_t *schema_sector_scan (void);
json_t *schema_planet_genesis (void);
json_t *schema_planet_info (void);
json_t *schema_planet_rename (void);
json_t *schema_planet_land (void);
json_t *schema_planet_launch (void);
json_t *schema_planet_transfer_ownership (void);
json_t *schema_planet_harvest (void);
json_t *schema_planet_deposit (void);
json_t *schema_planet_withdraw (void);
json_t *schema_planet_genesis_create (void);
json_t *schema_player_set_trade_account_preference (void);
json_t *schema_player_my_info (void);
json_t *schema_player_info (void);
json_t *schema_bank_balance (void);
json_t *schema_bank_history (void);
json_t *schema_bank_leaderboard (void);
json_t *schema_player_list_online_request (void);
json_t *schema_player_list_online_response (void);
json_t *schema_port_update (void);
json_t *schema_citadel_build (void);
json_t *schema_citadel_upgrade (void);
json_t *schema_combat_attack (void);
json_t *schema_combat_deploy_fighters (void);
json_t *schema_combat_lay_mines (void);
json_t *schema_combat_sweep_mines (void);
json_t *schema_combat_status (void);
json_t *schema_fighters_recall (void);
json_t *schema_combat_deploy_mines (void);
json_t *schema_mines_recall (void);
json_t *schema_deploy_fighters_list (void);
json_t *schema_deploy_mines_list (void);
json_t *schema_chat_send (void);
json_t *schema_chat_broadcast (void);
json_t *schema_chat_history (void);
json_t *schema_mail_send (void);
json_t *schema_mail_inbox (void);
json_t *schema_mail_read (void);
json_t *schema_mail_delete (void);
json_t *schema_sys_notice_create (void);
json_t *schema_notice_list (void);
json_t *schema_notice_ack (void);
json_t *schema_insurance_policies_list (void);
json_t *schema_insurance_policies_buy (void);
json_t *schema_insurance_claim_file (void);
json_t *schema_fine_list (void);
json_t *schema_fine_pay (void);
json_t *schema_news_read (void);
json_t *schema_subscribe_add (void);
json_t *schema_subscribe_remove (void);
json_t *schema_subscribe_list (void);
json_t *schema_subscribe_catalog (void);
json_t *schema_bulk_execute (void);

#endif
