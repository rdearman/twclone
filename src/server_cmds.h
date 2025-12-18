#ifndef SERVER_CMDS_H
#define SERVER_CMDS_H
#include <jansson.h>		/* for json_t */
#include <sqlite3.h>		/* for sqlite3 */
#include <pthread.h>		/* for pthread_mutex_t */
#include <sqlite3.h>
#include "server_loop.h"
// Helper for flexible JSON integer parsing
bool json_get_int_flexible (json_t * json, const char *key, int *out);
bool json_get_int64_flexible (json_t * json, const char *key, long long *out);

/* No JSON here on purpose: this layer does pure logic/DB.
   server_loop.c still builds/sends the envelopes. */
#ifdef __cplusplus
extern "C"
{
#endif
/* Return codes for auth/user ops (match your protocol doc where possible) */
  enum
  {
    AUTH_OK = 0,
    AUTH_ERR_BAD_REQUEST = 1301,	/* Missing required field(s) */
    AUTH_ERR_INVALID_CRED = 1220,	/* Invalid credentials */
    AUTH_ERR_NAME_TAKEN = 1205,	/* Username already exists */
    AUTH_ERR_DB = 1500		/* Database/internal error */
  };
/* Look up player by name, verify password, return player_id on success. */
  int play_login (const char *player_name,
		  const char *password, int *out_player_id);

/* Create a new player (auto-assigns legacy 'number' sequentially).
   Returns new player_id in out_player_id. */
  int user_create (sqlite3 * db, const char *player_name,
		   const char *password, int *out_player_id);
  typedef enum
  { DEC_OK = 0, DEC_REFUSED = 1, DEC_ERROR = 2 } decision_status_t;
  typedef struct
  {
    decision_status_t status;
    int code;			/* for REFUSED/ERROR */
    const char *message;	/* short msg */
  } decision_t;


  static inline decision_t ok (void)
  {
    return (decision_t)
    {
    DEC_OK, 0, NULL};
  }


  static inline decision_t refused (int code, const char *m)
  {
    return (decision_t)
    {
    DEC_REFUSED, code, m};
  }


  static inline decision_t err (int code, const char *m)
  {
    return (decision_t)
    {
    DEC_ERROR, code, m};
  }


#ifdef __cplusplus
}
#endif
// Add this line with the other database function declarations
int db_port_info_json (int port_id, json_t ** out_obj);
int cmd_sys_test_news_cron (client_ctx_t * ctx, json_t * root);
int cmd_sys_raw_sql_exec (client_ctx_t * ctx, json_t * root);
int cmd_sys_cluster_init (client_ctx_t * ctx, json_t * root);
int cmd_sys_cluster_seed_illegal_goods (client_ctx_t * ctx, json_t * root);
int cmd_sys_econ_port_status (client_ctx_t * ctx, json_t * root);
int cmd_sys_econ_orders_summary (client_ctx_t * ctx, json_t * root);
int cmd_sys_econ_planet_status (client_ctx_t * ctx, json_t * root);
int cmd_sys_npc_ferengi_tick_once (client_ctx_t * ctx, json_t * root);
int cmd_sys_player_reset (client_ctx_t * ctx, json_t * root);

int send_error_response (client_ctx_t * ctx,
			 json_t * root, int err_code, const char *msg);
int send_json_response (client_ctx_t * ctx, json_t * root,
			json_t * response_json);

#endif
