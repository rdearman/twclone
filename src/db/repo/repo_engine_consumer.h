#ifndef REPO_ENGINE_CONSUMER_H
#define REPO_ENGINE_CONSUMER_H

#include "db/db_api.h"
#include <stdint.h>

int repo_engine_load_watermark(db_t *db, const char *key, long long *last_id, long long *last_ts);
int repo_engine_save_watermark(db_t *db, const char *key, long long last_id, long long last_ts);
int repo_engine_fetch_max_event_id(db_t *db, long long *max_id);
int repo_engine_quarantine(db_t *db, int64_t id, int64_t ts, const char *type, const char *payload, const char *err_msg, int now_s);
int repo_engine_get_ship_id(db_t *db, int32_t player_id, int32_t *ship_id_out);
int repo_engine_get_ship_name(db_t *db, int32_t ship_id, char *name_out, size_t name_sz);
int repo_engine_fetch_events(db_t *db, int64_t last_id, int priority_only, const char *prio_json, int limit, db_res_t **out_res);

#endif
