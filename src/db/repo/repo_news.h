#ifndef REPO_NEWS_H
#define REPO_NEWS_H

#include "db/db_api.h"
#include <stdint.h>

int repo_news_get_unread(db_t *db, int32_t player_id, db_res_t **out_res);

int repo_news_get_recent(db_t *db, const char *epoch_expr, int32_t seconds_ago, db_res_t **out_res);

int repo_news_update_last_read(db_t *db, const char *now_expr, int32_t player_id);

int repo_news_post(db_t *db, const char *epoch_expr, const char *category, const char *body, int32_t author_id);

#endif
