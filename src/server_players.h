#ifndef SERVER_PLAYERS_H
#define SERVER_PLAYERS_H
#pragma once

#include "common.h"		// send_enveloped_*, json_t
#include "types.h"		// client_ctx_t (same struct used in server_loop.c)
#include <jansson.h>		/* -ljansson */
#include <stdbool.h>
#include <sqlite3.h>

#ifdef __cplusplus
extern "C"
{
#endif

  int cmd_player_set_settings (client_ctx_t * ctx, json_t * root);
  int cmd_player_get_settings (client_ctx_t * ctx, json_t * root);
  int cmd_player_set_prefs (client_ctx_t * ctx, json_t * root);
  int cmd_player_get_prefs (client_ctx_t * ctx, json_t * root);
  int cmd_player_set_topics (client_ctx_t * ctx, json_t * root);
  int cmd_player_get_topics (client_ctx_t * ctx, json_t * root);
  int cmd_player_set_bookmarks (client_ctx_t * ctx, json_t * root);
  int cmd_player_get_bookmarks (client_ctx_t * ctx, json_t * root);
  int cmd_player_set_avoids (client_ctx_t * ctx, json_t * root);
  int cmd_player_get_avoids (client_ctx_t * ctx, json_t * root);
  int cmd_player_get_notes (client_ctx_t * ctx, json_t * root);
  int cmd_player_my_info (client_ctx_t * ctx, json_t * root);
  int cmd_player_list_online (client_ctx_t * ctx, json_t * root);
  void cmd_nav_bookmark_add (client_ctx_t * ctx, json_t * root);
  void cmd_nav_bookmark_remove (client_ctx_t * ctx, json_t * root);
  void cmd_nav_bookmark_list (client_ctx_t * ctx, json_t * root);
  void cmd_nav_avoid_add (client_ctx_t * ctx, json_t * root);
  void cmd_nav_avoid_remove (client_ctx_t * ctx, json_t * root);
  void cmd_nav_avoid_list (client_ctx_t * ctx, json_t * root);



#ifdef __cplusplus
}
#endif

#endif				/* SERVER_PLAYERS_H */
