#include "server_sector_notices.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "db/repo/repo_sector_notices.h"
#include "game_db.h"
#include "server_communication.h"
#include "server_log.h"

enum { SECTOR_NOTICE_TTL_SECONDS = 7 * 24 * 60 * 60 };

static void
make_notice_key (client_ctx_t *ctx, json_t *root, char *out, size_t out_size)
{
  json_t *request_id = json_object_get (root, "id");
  char request_id_text[96] = { 0 };
  if (json_is_string (request_id))
    snprintf (request_id_text, sizeof request_id_text, "%s",
              json_string_value (request_id));
  else if (json_is_integer (request_id))
    snprintf (request_id_text, sizeof request_id_text, "%lld",
              (long long) json_integer_value (request_id));
  if (!request_id_text[0])
    return;

  json_t *command = json_object_get (root, "command");
  const char *command_name = json_is_string (command) ?
    json_string_value (command) : "sector.action";
  snprintf (out, out_size, "%d:%s:%s", ctx->player_id, command_name,
            request_id_text);
}

int
server_sector_notice_publish (client_ctx_t *ctx, json_t *root, int sector_id,
                              const char *subtype, json_t *details)
{
  if (!ctx || !root || ctx->player_id <= 0 || sector_id <= 0 || !subtype
      || !json_is_object (details))
    return -1;

  db_t *db = game_db_get_handle ();
  if (!db)
    return -1;

  int64_t now_s = (int64_t) time (NULL);
  int64_t expires_at = now_s + SECTOR_NOTICE_TTL_SECONDS;
  int64_t notice_id = 0;
  int created = 0;
  char notice_key[256] = { 0 };
  make_notice_key (ctx, root, notice_key, sizeof notice_key);
  int rc = repo_sector_notice_create (db, sector_id, ctx->player_id, subtype,
                                     details, now_s, expires_at,
                                     notice_key[0] ? notice_key : NULL,
                                     &notice_id, &created);
  if (rc != 0)
    {
      LOGE ("Could not persist sector notice %s for sector %d (rc=%d)",
            subtype, sector_id, rc);
      return rc;
    }

  /* Repeated request IDs resolve to one durable notice and one live event. */
  if (created)
    {
      json_t *event = json_object ();
      json_t *details_copy = json_deep_copy (details);
      if (!event || !details_copy)
        {
          if (event)
            json_decref (event);
          if (details_copy)
            json_decref (details_copy);
          return -1;
        }
      json_object_set_new (event, "notice_id", json_integer (notice_id));
      json_object_set_new (event, "sector_id", json_integer (sector_id));
      json_object_set_new (event, "subtype", json_string (subtype));
      json_object_set_new (event, "player_id", json_integer (ctx->player_id));
      json_object_set_new (event, "created_at", json_integer (now_s));
      json_object_set_new (event, "expires_at", json_integer (expires_at));
      json_object_set_new (event, "details", details_copy);
      server_broadcast_to_sector (sector_id, "sector.notice", event);
      json_decref (event);
    }
  return 0;
}
