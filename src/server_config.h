#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H
#include <stdint.h>
#include <jansson.h>
#include "common.h"		// client_ctx_t
#include "server_envelope.h"	// send_enveloped_* prototypes
#include "database.h"		// if your moved bodies call db_*
#include "schemas.h"		// uncomment if you wire system.describe_schema to schemas.c
#include <stdint.h>
// #define DEFAULT_DB_NAME "twconfig.db"
#define CORP_TAX_RATE_BP 500
extern json_t *g_capabilities;
#ifndef TW_CMD_DESC_T_DEFINED
#define TW_CMD_DESC_T_DEFINED
typedef struct cmd_desc_s
{
  const char *name;		// e.g. "move.warp"
  const char *summary;		// optional; may be ""
} cmd_desc_t;
#endif
// Exported by server_loop.c (or weak-fallback elsewhere)
void loop_get_supported_commands (const cmd_desc_t ** out_tbl, size_t *out_n);
json_t *loop_get_schema_for_command (const char *name);
json_t *loop_get_all_schema_keys (void);
const void *loop_get_command_entry (const char *name);	// Internal helper if needed
#ifdef __cplusplus
extern "C"
{
#endif
  extern struct twconfig *config_load (void);	// Declare config_load function
  struct twconfig
  {
    int turnsperday;
    int maxwarps_per_sector;
    int startingcredits;
    int startingfighters;
    int startingholds;
    int processinterval;
    int autosave;
    int max_players;
    int max_ships;
    int max_ports;
    int max_planets_per_sector;
    int max_total_planets;
    int max_safe_planets;
    int max_citadel_level;
    int number_of_planet_types;
    int max_ship_name_length;
    int ship_type_count;
    int hash_length;
    int default_port;
    int default_nodes;
    int warps_per_sector;
    int buff_size;
    int max_name_length;
    int planet_type_count;
    /* Shipyard Configuration */
    int shipyard_enabled;
    int shipyard_trade_in_factor_bp;
    int shipyard_require_cargo_fit;
    int shipyard_require_fighters_fit;
    int shipyard_require_shields_fit;
    int shipyard_require_hardware_compat;
    int shipyard_tax_bp;
  };


/* REPLACE THE TYPEDEF IN src/server_config.h WITH THIS */
  typedef struct
  {
    /* --- Basic Game Settings --- */
    int turnsperday;
    int maxwarps_per_sector;
    int startingfighters;
    int startingholds;
    int autosave;
    int max_ports;
    int max_planets_per_sector;
    int max_total_planets;
    int max_citadel_level;
    int number_of_planet_types;
    int ship_type_count;
    int hash_length;
    int default_nodes;
    int buff_size;
    int max_name_length;
    int planet_type_count;
    int server_port;


    struct
    {
      int tick_ms;
      int daily_align_sec;
      int processinterval;	/* Moved here from flat to match usage in apply_db */
    } engine;
    struct
    {
      int event_batch;
      int command_batch;
      int broadcast_batch;
    } batching;
    struct
    {
      int default_command_weight;
      int default_event_weight;
    } priorities;
    struct
    {
      char transport[8];
      char uds_path[256];
      char tcp_host[128];
      int tcp_port;
      int frame_size_limit;
    } s2s;
    struct
    {
      int connect_ms, handshake_ms, rpc_ms;
      int backoff_initial_ms, backoff_max_ms;
      double backoff_factor;
    } safety;
    struct
    {
      char key_id[64];
      unsigned char key[64];
      int key_len;
    } secrets;
    struct
    {
      struct
      {
	bool enabled;
	bool fedspace_allowed;
	bool msl_allowed;
	bool sweep_enabled;
	bool attack_enabled;
	int sweep_rate_mines_per_fighter;
	int sweep_rate_limpets_per_fighter_loss;
	int attack_rate_limpets_per_fighter;
	int attack_rate_limpets_per_fighter_loss;
	int entry_trigger_rate_limpets_per_fighter;
	int entry_damage_per_limpet;
	int limpet_ttl_days;
	int per_sector_cap;
	int max_per_ship;
	bool allow_multi_owner;
	int scrub_cost;
      } limpet;
    } mines;
    struct
    {
      int max_per_day;
      int xp_loss_flat;
      int xp_loss_percent;
      char drop_cargo[16];
      char drop_credits_mode[16];
      int big_sleep_duration_seconds;
      int big_sleep_clear_xp_below;
      char escape_pod_spawn_mode[32];
      int max_cloak_duration;	// Added to match DB config
    } death;
    struct
    {
      int turn_cost;
      double base_hit;
      double offense_coeff;
      double defense_coeff;


      struct
      {
	double engine_weight;
	double mass_weight;
      } flee;
      int neutral_band;		// Added to match DB config
    } combat;
    struct
    {
      bool enabled;
      double shield_rate_pct_per_tick;
      int tick_seconds;
    } regen;
    /* --- NEWLY ADDED FIELDS TO MATCH DB SCHEMA --- */
    int64_t startingcredits;
    int64_t corporation_creation_fee;
    int64_t bank_alert_threshold_player;
    int64_t bank_alert_threshold_corp;
    /* Genesis Config */
    int genesis_enabled;
    int genesis_block_at_cap;
    int genesis_navhaz_delta;
    int genesis_class_weight_M;
    int genesis_class_weight_K;
    int genesis_class_weight_O;
    int genesis_class_weight_L;
    int genesis_class_weight_C;
    int genesis_class_weight_H;
    int genesis_class_weight_U;
    /* Shipyard Config */
    int shipyard_enabled;
    int shipyard_trade_in_factor_bp;
    int shipyard_require_cargo_fit;
    int shipyard_require_fighters_fit;
    int shipyard_require_shields_fit;
    int shipyard_require_hardware_compat;
    int shipyard_tax_bp;
    /* Misc */
    int illegal_allowed_neutral;
    int max_ship_name_length;	/* Used in stardock */
    int num_sectors;		/* Added to match DB config */
    int planet_treasury_interest_rate_bps;	/* Added to match DB config */
    int64_t bank_min_balance_for_interest;	/* Added to match DB config */
    int64_t bank_max_daily_interest_per_account;	/* Added to match DB config */
  } server_config_t;
/* Single global instance (defined in server_config.c) */
  extern server_config_t g_cfg;


/* Loader name (you said load_config() conflicted elsewhere) */
  int load_eng_config (void);
  void print_effective_config_redacted (void);
  int cmd_system_capabilities (client_ctx_t * ctx, json_t * root);
  int cmd_system_describe_schema (client_ctx_t * ctx, json_t * root);	// optional, if you expose it
  int cmd_session_ping (client_ctx_t * ctx, json_t * root);
  int cmd_session_hello (client_ctx_t * ctx, json_t * root);
  int cmd_system_hello (client_ctx_t * ctx, json_t * root);
  int cmd_session_disconnect (client_ctx_t * ctx, json_t * root);
#endif
