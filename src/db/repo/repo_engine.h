#ifndef REPO_ENGINE_H
#define REPO_ENGINE_H

#include "db/db_api.h"
#include <stdint.h>

int repo_engine_get_config_int(db_t *db, const char *key, int *value_out);
int repo_engine_get_alignment_band_info(db_t *db, int band_id, int *is_good_out, int *is_evil_out);
int repo_engine_create_broadcast_notice(db_t *db, const char *ts_fmt, int64_t now_s, const char *title, const char *body, const char *severity, int64_t expires_at, int64_t *new_id_out);
int repo_engine_publish_notice(db_t *db, const char *ts_fmt, int64_t now_s, const char *scope, int player_id, const char *message, const char *severity, int64_t expires_at, int64_t *new_id_out);
db_res_t* repo_engine_get_ready_commands(db_t *db, int64_t now_s, int max_rows, db_error_t *err);
int repo_engine_mark_command_running(db_t *db, const char *ts_fmt, int64_t now_s, int64_t cmd_id);
int repo_engine_mark_command_done(db_t *db, const char *ts_fmt, int64_t now_s, int64_t cmd_id);
int repo_engine_mark_command_error(db_t *db, const char *ts_fmt, int64_t now_s, int64_t cmd_id);
int repo_engine_reclaim_stale_locks(db_t *db, int64_t stale_threshold_ms);
db_res_t* repo_engine_get_pending_cron_tasks(db_t *db, const char *ts_expr, int64_t now_s, int limit, db_error_t *err);
int repo_engine_update_cron_task_schedule(db_t *db, const char *ts_expr1, const char *ts_expr2, int64_t id, int64_t now_s, int64_t next_due);
int repo_engine_sweep_expired_notices(db_t *db, const char *ts_fmt, int64_t now_s);
db_res_t* repo_engine_get_retryable_commands(db_t *db, int max_retries, db_error_t *err);
int repo_engine_reschedule_deadletter(db_t *db, int64_t now_s, int64_t cmd_id, int attempts);
int repo_engine_cleanup_expired_limpets(db_t *db, const char *deployed_as_epoch, int asset_type, int64_t threshold_s);
db_res_t* repo_engine_get_active_interest_accounts(db_t *db, db_error_t *err);
int repo_engine_update_last_interest_tick(db_t *db, int current_epoch_day, int account_id);

#endif // REPO_ENGINE_H
