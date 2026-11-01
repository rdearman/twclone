#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_communication.h"
#include "db/sql_driver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int repo_comm_create_system_notice(db_t *db, int64_t created_at, const char *title, const char *body, const char *severity, int64_t expires_at, int64_t *new_id_out) {
    db_error_t err = { 0 };
    /* SQL_VERBATIM: Q1 */
    const char *q1 = "INSERT INTO system_notice (created_at, title, body, severity, expires_at) "
    "VALUES ({1}, {2}, {3}, {4}, {5})";
    char sql[256]; sql_build(db, q1, sql, sizeof(sql));
    db_bind_t params[5] = { 
        db_bind_timestamp_text(created_at), 
        db_bind_text(title), 
        db_bind_text(body), 
        db_bind_text(severity), 
        expires_at > 0 ? db_bind_timestamp_text(expires_at) : db_bind_null() 
    };
    if (!db_exec_insert_id (db, sql, params, 5, "system_notice_id", new_id_out, &err)) return err.code;
    return 0;
}

int repo_comm_delete_system_notice(db_t *db, int notice_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q_COMM_DELETE_NOTICE */
    const char *q_del = "DELETE FROM system_notice WHERE system_notice_id = {1};";
    char sql[256]; sql_build(db, q_del, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(notice_id) }, 1, &err)) return err.code;
    return 0;
}

db_res_t* repo_comm_list_notices(db_t *db, const char *now_expr, int player_id, int include_expired, int limit, const char *cursor_ts, int64_t cursor_id, db_error_t *err) {
    char sql[768];
    /* SQL_VERBATIM: Q2 */
    char sql_tmpl[768];
    snprintf(sql_tmpl, sizeof(sql_tmpl),
        "SELECT n.system_notice_id, n.title, n.body, n.severity, n.created_at, n.expires_at, s.seen_at, n.scope, n.sector_id, n.meta "
        "FROM system_notice n "
        "LEFT JOIN notice_seen s ON s.notice_id = n.system_notice_id AND s.player_id = {1} "
        "WHERE ({2} = 1 OR n.expires_at IS NULL OR n.expires_at > %s) "
        "AND (n.scope = 'global' OR (n.scope = 'player' AND n.player_id = {1}) "
        "OR (n.scope = 'sector' AND n.sector_id = (SELECT sector_id FROM players WHERE player_id = {1}))) "
        "AND ({4} = 0 OR n.created_at < {5} OR (n.created_at = {5} AND n.system_notice_id < {6})) "
        "ORDER BY n.created_at DESC, n.system_notice_id DESC LIMIT {3};",
        now_expr);
    sql_build(db, sql_tmpl, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id), db_bind_i64(include_expired), db_bind_i64(limit), db_bind_i64(cursor_id > 0), db_bind_timestamp_native(cursor_ts), db_bind_i64(cursor_id) }, 6, &res, err);
    return res;
}

int repo_comm_mark_notice_seen(db_t *db, int notice_id, int player_id, int64_t seen_at) {
    db_error_t err;
    int64_t rows = 0;
    db_bind_t params[] = { db_bind_i64(notice_id), db_bind_i64(player_id), db_bind_timestamp_text(seen_at) };

    /* 1. Try Update first */
    const char *q_upd = "UPDATE notice_seen SET seen_at = {3} WHERE notice_id = {1} AND player_id = {2};";
    char sql_upd[256]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    if (db_exec_rows_affected(db, sql_upd, params, 3, &rows, &err) && rows > 0) return 0;

    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO notice_seen (notice_id, player_id, seen_at) VALUES ({1}, {2}, {3});";
    char sql_ins[256]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    if (!db_exec(db, sql_ins, params, 3, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, params, 3, &err)) return 0;
        }
        return err.code;
    }
    return 0;
}

int repo_comm_get_player_id_by_name(db_t *db, const char *name, int *player_id_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q4 */
    const char *q4 = "SELECT player_id FROM players WHERE lower(name) = lower({1}) LIMIT 1;";
    char sql[256]; sql_build(db, q4, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_text(name) }, 1, &res, &err) && db_res_step(res, &err)) {
        *player_id_out = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

int repo_comm_check_player_blocked(db_t *db, int blocker_id, int blocked_id, int *is_blocked_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q5 */
    const char *q5 = "SELECT 1 FROM player_block WHERE blocker_id={1} AND blocked_id={2} LIMIT 1;";
    char sql[256]; sql_build(db, q5, sql, sizeof(sql));
    *is_blocked_out = 0;
    if (db_query (db, sql, (db_bind_t[]){ db_bind_i64(blocker_id), db_bind_i64(blocked_id) }, 2, &res, &err) && db_res_step(res, &err)) {
        *is_blocked_out = 1;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code;
}

int repo_comm_get_mail_id_by_idem(db_t *db, const char *idem, int recipient_id, int64_t *mail_id_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q6 */
    const char *q6 = "SELECT mail_id FROM mail WHERE idempotency_key={1} AND recipient_id={2} LIMIT 1;";
    char sql[256]; sql_build(db, q6, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_text(idem), db_bind_i64(recipient_id) }, 2, &res, &err) && db_res_step(res, &err)) {
        *mail_id_out = db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

int repo_comm_insert_mail(db_t *db, int sender_id, int recipient_id, const char *subject, const char *body, const char *idem, int64_t *new_id_out) {
    db_error_t err;
    int64_t now_ts = (int64_t)time(NULL);
    /* SQL_VERBATIM: Q7 */
    const char *q7 = "INSERT INTO mail(sender_id, recipient_id, subject, body, idempotency_key, sent_at) "
    "VALUES({1},{2},{3},{4},{5},{6})";
    char sql[256]; sql_build(db, q7, sql, sizeof(sql));
    db_bind_t p[6] = { 
        db_bind_i64(sender_id), 
        db_bind_i64(recipient_id), 
        subject ? db_bind_text(subject) : db_bind_null(), 
        db_bind_text(body), 
        idem ? db_bind_text(idem) : db_bind_null(),
        db_bind_timestamp_text(now_ts)
    };
    if (!db_exec_insert_id(db, sql, p, 6, "mail_id", new_id_out, &err)) return err.code;
    return 0;
}

db_res_t* repo_comm_list_inbox(db_t *db, int recipient_id, int after_id, int limit, db_error_t *err) {
    char sql[512];
    /* SQL_VERBATIM: Q9 */
    const char *q9 = "SELECT m.mail_id, m.thread_id, m.sender_id, p.name, m.subject, m.sent_at, m.read_at "
    "FROM mail m JOIN players p ON m.sender_id = p.player_id "
    "WHERE m.recipient_id={1} AND m.deleted=FALSE AND m.archived=FALSE "
    "  AND ({2}=0 OR m.mail_id<{2}) " "ORDER BY m.mail_id DESC " "LIMIT {3};";
    sql_build(db, q9, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(recipient_id), db_bind_i64(after_id), db_bind_i64(limit) }, 3, &res, err);
    return res;
}

db_res_t* repo_comm_get_mail_details(db_t *db, int mail_id, int recipient_id, db_error_t *err) {
    char sql[512];
    /* SQL_VERBATIM: Q10 */
    const char *q10 = "SELECT m.mail_id, m.thread_id, m.sender_id, p.name, m.subject, m.body, m.sent_at, m.read_at "
    "FROM mail m JOIN players p ON m.sender_id = p.player_id WHERE m.mail_id={1} AND m.recipient_id={2} AND m.deleted=FALSE;";
    sql_build(db, q10, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(mail_id), db_bind_i64(recipient_id) }, 2, &res, err);
    return res;
}

int repo_comm_mark_mail_read(db_t *db, int mail_id, int64_t read_at) {
    db_error_t err;
    /* SQL_VERBATIM: Q11 */
    const char *q11 = "UPDATE mail SET read_at={1} WHERE mail_id={2};";
    char sql[256]; sql_build(db, q11, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_timestamp_text(read_at), db_bind_i64(mail_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_comm_delete_mail_bulk(db_t *db, int recipient_id, const int *mail_ids, int n_ids, int64_t *rows_affected) {
    if (n_ids == 0) return 0;
    
    char sql[4096];
    char *p = sql;
    /* SQL_VERBATIM: Q12 */
    p += snprintf (p, sizeof (sql), "UPDATE mail SET deleted=TRUE WHERE recipient_id={1} AND mail_id IN (");
    for (int i = 0; i < n_ids; i++) {
        p += snprintf (p, (size_t) (sql + sizeof (sql) - p), (i ? ",{%d}" : "{%d}"), i + 2);
    }
    p += snprintf (p, (size_t) (sql + sizeof (sql) - p), ");");

    db_bind_t *params = malloc ((n_ids + 1) * sizeof (db_bind_t));
    if (!params) return -1;

    params[0] = db_bind_i64 (recipient_id);
    for (int i = 0; i < n_ids; i++) {
        params[i + 1] = db_bind_i64 (mail_ids[i]);
    }

    db_error_t err;
    bool ok = db_exec_rows_affected(db, sql, params, n_ids + 1, rows_affected, &err);
    free (params);
    
    return ok ? 0 : err.code;
}

int repo_comm_get_subscription_count(db_t *db, int player_id, int *count_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q13 */
    const char *q13 = "SELECT COUNT(*) FROM subscriptions WHERE player_id={1} AND enabled=TRUE;";
    char sql[256]; sql_build(db, q13, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *count_out = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

db_res_t* repo_comm_list_subscriptions(db_t *db, int player_id, db_error_t *err) {
    char sql[256];
    /* SQL_VERBATIM: Q14 */
    const char *q14 = "SELECT event_type, locked, enabled, delivery, filter_json FROM subscriptions WHERE player_id = {1};";
    sql_build(db, q14, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, err);
    return res;
}

static int
repo_comm_event_ensure_stream (db_t *db, int player_id)
{
    char sql[256];
    db_error_t err;
    sql_build (db,
               "INSERT INTO player_event_streams (player_id) VALUES ({1}) "
               "ON CONFLICT (player_id) DO NOTHING;",
               sql, sizeof sql);
    if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i64 (player_id) }, 1, &err))
        return err.code ? err.code : -1;
    return 0;
}

static int
repo_comm_event_read_stream (db_t *db, int player_id, int lock_row,
                             char stream_id_out[37], int64_t *last_out,
                             int64_t *pruned_out)
{
    char sql[384];
    db_res_t *res = NULL;
    db_error_t err;
    const char *tail = lock_row ? " FOR UPDATE" : "";
    char query[256];

    snprintf (query, sizeof query,
              "SELECT stream_id::text, last_sequence, "
              "pruned_through_sequence FROM player_event_streams "
              "WHERE player_id = {1}%s;", tail);
    sql_build (db, query, sql, sizeof sql);
    if (!db_query (db, sql, (db_bind_t[]){ db_bind_i64 (player_id) }, 1,
                   &res, &err))
        return err.code ? err.code : -1;
    if (!db_res_step (res, &err))
      {
        db_res_finalize (res);
        return err.code ? err.code : -1;
      }

    const char *stream_id = db_res_col_text (res, 0, &err);
    if (!stream_id || strlen (stream_id) != 36)
      {
        db_res_finalize (res);
        return -1;
      }
    memcpy (stream_id_out, stream_id, 37);
    *last_out = db_res_col_i64 (res, 1, &err);
    *pruned_out = db_res_col_i64 (res, 2, &err);
    db_res_finalize (res);
    return err.code ? err.code : 0;
}

int
repo_comm_event_stream_state (db_t *db, int player_id,
                              char stream_id_out[37],
                              int64_t *last_sequence_out,
                              int64_t *pruned_through_out)
{
    if (!db || player_id <= 0 || !stream_id_out || !last_sequence_out
        || !pruned_through_out)
        return -1;
    int rc = repo_comm_event_ensure_stream (db, player_id);
    if (rc != 0)
        return rc;
    return repo_comm_event_read_stream (db, player_id, 0, stream_id_out,
                                       last_sequence_out,
                                       pruned_through_out);
}

int
repo_comm_event_append (db_t *db, int player_id, const char *event_id,
                        const char *event_type, const char *topic,
                        const char *payload_json, char stream_id_out[37],
                        int64_t *sequence_out, char timestamp_out[32])
{
    if (!db || player_id <= 0 || !event_id || strlen (event_id) != 36
        || !event_type || !*event_type || !payload_json || !stream_id_out
        || !sequence_out || !timestamp_out)
        return -1;

    db_error_t err;
    if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
        return err.code ? err.code : -1;

    int rc = repo_comm_event_ensure_stream (db, player_id);
    if (rc != 0)
      {
        db_tx_rollback (db, NULL);
        return rc;
      }

    int64_t last = 0, pruned = 0;
    char stream_id[37];
    rc = repo_comm_event_read_stream (db, player_id, 1, stream_id, &last,
                                      &pruned);
    (void) pruned;
    if (rc != 0 || last == INT64_MAX)
      {
        db_tx_rollback (db, NULL);
        return rc != 0 ? rc : -1;
      }
    const int64_t sequence = last + 1;

    char sql[512];
    sql_build (db,
               "UPDATE player_event_streams SET last_sequence = {2} "
               "WHERE player_id = {1};",
               sql, sizeof sql);
    if (!db_exec (db, sql,
                  (db_bind_t[]){ db_bind_i64 (player_id),
                                 db_bind_i64 (sequence) },
                  2, &err))
      {
        db_tx_rollback (db, NULL);
        return err.code ? err.code : -1;
      }

    sql_build (db,
               "INSERT INTO player_events "
               "(event_id, player_id, stream_id, sequence, event_type, "
               "topic, payload) VALUES ({1}::uuid, {2}, {3}::uuid, {4}, {5}, {6}, "
               "{7}::jsonb) RETURNING to_char(created_at AT TIME ZONE 'UTC', "
               "'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"');",
               sql, sizeof sql);
    db_bind_t params[7] = {
      db_bind_text (event_id), db_bind_i64 (player_id),
      db_bind_text (stream_id), db_bind_i64 (sequence),
      db_bind_text (event_type), topic ? db_bind_text (topic) : db_bind_null (),
      db_bind_json (payload_json)
    };
    db_res_t *inserted = NULL;
    if (!db_query (db, sql, params, 7, &inserted, &err))
      {
        db_tx_rollback (db, NULL);
        return err.code ? err.code : -1;
      }
    if (!db_res_step (inserted, &err))
      {
        db_res_finalize (inserted);
        db_tx_rollback (db, NULL);
        return err.code ? err.code : -1;
      }
    const char *created_at = db_res_col_text (inserted, 0, &err);
    if (!created_at || strlen (created_at) >= 32)
      {
        db_res_finalize (inserted);
        db_tx_rollback (db, NULL);
        return err.code ? err.code : -1;
      }
    strcpy (timestamp_out, created_at);
    db_res_finalize (inserted);
    if (err.code)
      {
        db_tx_rollback (db, NULL);
        return err.code;
      }
    if (!db_tx_commit (db, &err))
      {
        db_tx_rollback (db, NULL);
        return err.code ? err.code : -1;
      }

    memcpy (stream_id_out, stream_id, sizeof stream_id);
    *sequence_out = sequence;
    return 0;
}

db_res_t *
repo_comm_event_page (db_t *db, int player_id, const char *stream_id,
                      int64_t after_sequence, int64_t through_sequence,
                      int limit, db_error_t *err)
{
    if (!db || player_id <= 0 || !stream_id || strlen (stream_id) != 36
        || after_sequence < 0 || through_sequence < after_sequence
        || limit <= 0 || !err)
        return NULL;
    char sql[1024];
    const char *query =
      "SELECT event_id::text, event_type, payload::text, "
      "to_char(created_at AT TIME ZONE 'UTC', "
      "'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), topic, sequence "
      "FROM player_events WHERE player_id = {1} AND stream_id = {2}::uuid "
      "AND sequence > {3} AND sequence <= {4} "
      "AND expires_at > CURRENT_TIMESTAMP "
      "ORDER BY sequence ASC LIMIT {5};";
    sql_build (db, query, sql, sizeof sql);
    db_res_t *res = NULL;
    if (!db_query (db, sql,
                   (db_bind_t[]){ db_bind_i64 (player_id),
                                  db_bind_text (stream_id),
                                  db_bind_i64 (after_sequence),
                                  db_bind_i64 (through_sequence),
                                  db_bind_i64 (limit) },
                   5, &res, err))
        return NULL;
    return res;
}

int
repo_comm_event_prune (db_t *db, int max_streams, int64_t *deleted_out)
{
    if (!db || max_streams <= 0 || !deleted_out)
        return -1;
    *deleted_out = 0;
    db_error_t err = { 0 };
    if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
        return err.code ? err.code : -1;

    char sql[1024], candidate_template[1024], lock_of[64], locking[96];
    const char *skip_locked = sql_for_update_skip_locked (db);
    if (!skip_locked)
      {
        db_tx_rollback (db, NULL);
        return -1;
      }
    if (db_backend (db) == DB_BACKEND_POSTGRES)
      {
        if (sql_for_update_of (db, "s", lock_of, sizeof lock_of) != 0
            || snprintf (locking, sizeof locking, "%s SKIP LOCKED", lock_of)
                 >= (int) sizeof locking)
          {
            db_tx_rollback (db, NULL);
            return -1;
          }
      }
    else
      {
        if (snprintf (locking, sizeof locking, "%s", skip_locked)
            >= (int) sizeof locking)
          {
            db_tx_rollback (db, NULL);
            return -1;
          }
      }
    const char *candidate_query =
      "SELECT s.player_id, s.last_sequence, s.pruned_through_sequence "
      "FROM player_event_streams s WHERE s.pruned_through_sequence < "
      "s.last_sequence AND EXISTS (SELECT 1 FROM player_events e "
      "WHERE e.player_id = s.player_id AND e.sequence > "
      "s.pruned_through_sequence AND e.expires_at <= CURRENT_TIMESTAMP) "
      "ORDER BY s.player_id LIMIT {1}%s;";
    if (snprintf (candidate_template, sizeof candidate_template,
                  candidate_query, locking)
        >= (int) sizeof candidate_template
        || sql_build (db, candidate_template, sql, sizeof sql) != 0)
      {
        db_tx_rollback (db, NULL);
        return -1;
      }
    db_res_t *streams = NULL;
    if (!db_query (db, sql, (db_bind_t[]){ db_bind_i64 (max_streams) }, 1,
                   &streams, &err))
      {
        db_tx_rollback (db, NULL);
        return err.code ? err.code : -1;
      }

    int rc = 0;
    while (db_res_step (streams, &err))
      {
        int player_id = db_res_col_i32 (streams, 0, &err);
        int64_t last = db_res_col_i64 (streams, 1, &err);
        int64_t pruned = db_res_col_i64 (streams, 2, &err);
        if (err.code || player_id <= 0 || pruned >= last)
          {
            rc = err.code ? err.code : -1;
            break;
          }

        char min_sql[512];
        sql_build (db,
                   "SELECT MIN(sequence) FROM player_events "
                   "WHERE player_id = {1} AND sequence > {2} "
                   "AND expires_at > CURRENT_TIMESTAMP;",
                   min_sql, sizeof min_sql);
        db_res_t *min_res = NULL;
        if (!db_query (db, min_sql,
                       (db_bind_t[]){ db_bind_i64 (player_id),
                                      db_bind_i64 (pruned) },
                       2, &min_res, &err))
          {
            rc = err.code ? err.code : -1;
            break;
          }
        int64_t min_live = 0;
        if (!db_res_step (min_res, &err))
          {
            db_res_finalize (min_res);
            rc = err.code ? err.code : -1;
            break;
          }
        if (db_res_col_is_null (min_res, 0))
          min_live = 0;
        else
          min_live = db_res_col_i64 (min_res, 0, &err);
        db_res_finalize (min_res);
        if (err.code)
          {
            rc = err.code;
            break;
          }

        int64_t new_pruned = min_live > 0 ? min_live - 1 : last;
        if (new_pruned <= pruned)
          continue;
        char delete_sql[512];
        sql_build (db,
                   "DELETE FROM player_events WHERE player_id = {1} "
                   "AND sequence <= {2} AND expires_at <= CURRENT_TIMESTAMP;",
                   delete_sql, sizeof delete_sql);
        int64_t affected = 0;
        if (!db_exec_rows_affected (db, delete_sql,
                                   (db_bind_t[]){ db_bind_i64 (player_id),
                                                  db_bind_i64 (new_pruned) },
                                   2, &affected, &err))
          {
            rc = err.code ? err.code : -1;
            break;
          }
        char update_sql[512];
        sql_build (db,
                   "UPDATE player_event_streams SET "
                   "pruned_through_sequence = {2} WHERE player_id = {1};",
                   update_sql, sizeof update_sql);
        if (!db_exec (db, update_sql,
                      (db_bind_t[]){ db_bind_i64 (player_id),
                                     db_bind_i64 (new_pruned) },
                      2, &err))
          {
            rc = err.code ? err.code : -1;
            break;
          }
        *deleted_out += affected;
      }
    db_res_finalize (streams);
    if (rc != 0)
      {
        db_tx_rollback (db, NULL);
        return rc;
      }
    if (!db_tx_commit (db, &err))
      {
        db_tx_rollback (db, NULL);
        return err.code ? err.code : -1;
      }
    return 0;
}

int repo_comm_insert_chat(db_t *db, int sender_id, int recipient_id, int sector_id, const char *message, int64_t *new_id_out) {
    db_error_t err;
    /* SQL_VERBATIM: Q15 */
    const char *q15 = "INSERT INTO chat (sender_id, recipient_id, sector_id, message) VALUES ({1}, {2}, {3}, {4})";
    char sql[512]; sql_build(db, q15, sql, sizeof(sql));
    db_bind_t p[4] = {
        db_bind_i64(sender_id),
        recipient_id > 0 ? db_bind_i64(recipient_id) : db_bind_null(),
        sector_id > 0 ? db_bind_i64(sector_id) : db_bind_null(),
        db_bind_text(message)
    };
    if (!db_exec_insert_id(db, sql, p, 4, "chat_id", new_id_out, &err)) {
        return err.code;
    }
    return 0;
}

db_res_t* repo_comm_list_chat(db_t *db, int player_id, int sector_id, int limit, db_error_t *err) {
    /* SQL_VERBATIM: Q16 */
    const char *q16 = 
        "SELECT c.chat_id, c.sender_id, p.name as sender_name, c.recipient_id, r.name as recipient_name, c.sector_id, c.message, c.sent_at "
        "FROM chat c "
        "JOIN players p ON c.sender_id = p.player_id "
        "LEFT JOIN players r ON c.recipient_id = r.player_id "
        "WHERE (c.recipient_id IS NULL AND (c.sector_id IS NULL OR c.sector_id = {1})) "
        "   OR c.recipient_id = {2} OR c.sender_id = {2} "
        "ORDER BY c.chat_id DESC LIMIT {3};";
    char sql[1024]; sql_build(db, q16, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(sector_id), db_bind_i64(player_id), db_bind_i64(limit) }, 3, &res, err);
    return res;
}
