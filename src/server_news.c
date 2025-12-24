/* src/server_news.c */
#include <jansson.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

/* local includes */
#include "server_news.h"
#include "server_envelope.h"
#include "db_player_settings.h"
#include "database.h"
#include "game_db.h"
#include "server_log.h"
#include "server_players.h"
#include "db/db_api.h"

int cmd_news_get_feed (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle(); if (!db) return -1;
  json_t *news = NULL; if (db_news_get_recent(ctx->player_id, &news) == 0) {
      json_t *payload = json_object(); json_object_set_new(payload, "articles", news ?: json_array());
      send_response_ok_take(ctx, root, "news.feed", &payload);
  } else send_response_error(ctx, root, ERR_NOT_FOUND, "Failed");
  return 0;
}

int cmd_news_mark_feed_read (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}

int cmd_news_read (client_ctx_t *ctx, json_t *root) { return cmd_news_get_feed(ctx, root); }
int news_post (const char *body, const char *cat, int aid) { (void)body; (void)cat; (void)aid; return 0; }