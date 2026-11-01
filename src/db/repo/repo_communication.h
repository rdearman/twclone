#ifndef REPO_COMMUNICATION_H
#define REPO_COMMUNICATION_H

#include "db/db_api.h"
#include <stdint.h>

int repo_comm_create_system_notice(db_t *db, int64_t created_at, const char *title, const char *body, const char *severity, int64_t expires_at, int64_t *new_id_out);
int repo_comm_delete_system_notice(db_t *db, int notice_id);
db_res_t* repo_comm_list_notices(db_t *db, const char *now_expr, int player_id, int include_expired, int limit, const char *cursor_ts, int64_t cursor_id, db_error_t *err);
int repo_comm_mark_notice_seen(db_t *db, int notice_id, int player_id, int64_t seen_at);
int repo_comm_get_player_id_by_name(db_t *db, const char *name, int *player_id_out);
int repo_comm_check_player_blocked(db_t *db, int blocker_id, int blocked_id, int *is_blocked_out);
int repo_comm_get_mail_id_by_idem(db_t *db, const char *idem, int recipient_id, int64_t *mail_id_out);
int repo_comm_insert_mail(db_t *db, int sender_id, int recipient_id, const char *subject, const char *body, const char *idem, int64_t *new_id_out);
db_res_t* repo_comm_list_inbox(db_t *db, int recipient_id, int after_id, int limit, db_error_t *err);
db_res_t* repo_comm_get_mail_details(db_t *db, int mail_id, int recipient_id, db_error_t *err);
int repo_comm_mark_mail_read(db_t *db, int mail_id, int64_t read_at);
int repo_comm_delete_mail_bulk(db_t *db, int recipient_id, const int *mail_ids, int n_ids, int64_t *rows_affected);
int repo_comm_get_subscription_count(db_t *db, int player_id, int *count_out);
db_res_t* repo_comm_list_subscriptions(db_t *db, int player_id, db_error_t *err);

/* Durable per-player event journal. stream_id_out must hold at least 37 bytes.
   The page columns are event_id, event_type, payload::text, created_at UTC,
   topic, sequence. */
int repo_comm_event_stream_state(db_t *db, int player_id,
                                 char stream_id_out[37],
                                 int64_t *last_sequence_out,
                                 int64_t *pruned_through_out);
int repo_comm_event_append(db_t *db, int player_id, const char *event_id,
                           const char *event_type, const char *topic,
                           const char *payload_json,
                           char stream_id_out[37],
                           int64_t *sequence_out, char timestamp_out[32]);
db_res_t *repo_comm_event_page(db_t *db, int player_id,
                               const char *stream_id, int64_t after_sequence,
                               int64_t through_sequence, int limit,
                               db_error_t *err);
int repo_comm_event_prune(db_t *db, int max_streams, int64_t *deleted_out);

/* Chat */
int repo_comm_insert_chat(db_t *db, int sender_id, int recipient_id, int sector_id, const char *message, int64_t *new_id_out);
db_res_t* repo_comm_list_chat(db_t *db, int player_id, int sector_id, int limit, db_error_t *err);

#endif // REPO_COMMUNICATION_H
