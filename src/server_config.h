#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

#include <stdint.h>
#include <jansson.h>
#include "common.h"		// client_ctx_t
#include "server_envelope.h"	// send_enveloped_* prototypes
#include "database.h"		// if your moved bodies call db_*
// #include "schemas.h"      // uncomment if you wire system.describe_schema to schemas.c
#include <stdint.h>

#define DEFAULT_DB_NAME "twconfig.db"

struct twconfig;
struct twconfig *config_load (void);

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

#ifdef __cplusplus
extern "C"
{
#endif

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
  };

  typedef struct
  {
    struct
    {
      int tick_ms;
      int daily_align_sec;
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
      char transport[8];	/* "uds" | "tcp" */
      char uds_path[256];
      char tcp_host[128];
      int tcp_port;
      int frame_size_limit;	/* bytes */
    } s2s;
    struct
    {
      int connect_ms, handshake_ms, rpc_ms;
      int backoff_initial_ms, backoff_max_ms;
      double backoff_factor;
    } safety;
    struct
    {
      char key_id[64];		/* shown as redacted in printout */
      unsigned char key[64];	/* if you decode b64, optional */
      int key_len;
    } secrets;
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



#endif				/* CONFIG_H */
