#include "server_players.h"
#include "database.h"		// play_login, user_create, db_player_info_json, db_player_get_sector, db_session_*
#include "errors.h"
#include "config.h"
#include <string.h>
#include "server_cmds.h"
#include "server_rules.h"



int
cmd_player_rankings (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.rankings");
}

int
cmd_player_set_prefs (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_prefs");
}


int
cmd_player_my_info (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
    }
  else
    {
      json_t *pinfo = NULL;
      int prc = db_player_info_json (ctx->player_id, &pinfo);
      if (prc != SQLITE_OK || !pinfo)
	{
	  send_enveloped_error (ctx->fd, root, 1503, "Database error");
	  return;
	}
      send_enveloped_ok (ctx->fd, root, "player.info", pinfo);
      json_decref (pinfo);
    }
  return 0;
}

int
cmd_player_list_online (client_ctx_t *ctx, json_t *root)
{
  /*Until you maintain a global connection registry,
     return the current player only (it unblocks clients and is safe).
     You can upgrade later to a real list. */

  json_t *arr = json_array ();
  if (ctx->player_id > 0)
    {
      json_array_append_new (arr,
			     json_pack ("{s:i}", "player_id",
					ctx->player_id));
    }
  json_t *data = json_pack ("{s:o}", "players", arr);
  send_enveloped_ok (ctx->fd, root, "player.list_online", data);
  json_decref (data);
  // ← paste the player.list_online block. :contentReference[oaicite:11]{index=11}
  return 0;
}
