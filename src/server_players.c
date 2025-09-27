#include "server_players.h"
#include "database.h"   // play_login, user_create, db_player_info_json, db_player_get_sector, db_session_*
#include "errors.h"
#include "config.h"
#include <string.h>
#include "server_cmds.h"



int cmd_player_my_info(client_ctx_t* ctx, json_t* root) {
  // ← paste the player.my_info block that returns "player.info". :contentReference[oaicite:10]{index=10}
  return 0;
}

int cmd_player_list_online(client_ctx_t* ctx, json_t* root) {
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
