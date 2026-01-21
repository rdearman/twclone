#ifndef REPO_COMMUNICATION_H
#define REPO_COMMUNICATION_H

#include "db/db_api.h"
#include <stdint.h>

int repo_comm_create_system_notice(db_t *db, int64_t created_at, const char *title, const char *body, const char *severity, int64_t expires_at, int64_t *new_id_out);
int repo_comm_delete_system_notice(db_t *db, int notice_id);
db_res_t* repo_comm_list_notices(db_t *db, const char *now_expr, int player_id, int include_expired, int limit, db_error_t *err);
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

/* Chat */
int repo_comm_insert_chat(db_t *db, int sender_id, int recipient_id, int sector_id, const char *message, int64_t *new_id_out);
db_res_t* repo_comm_list_chat(db_t *db, int player_id, int sector_id, int limit, db_error_t *err);

#endif // REPO_COMMUNICATION_H
