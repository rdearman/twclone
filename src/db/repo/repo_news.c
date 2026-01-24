#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_news.h"
#include "db/sql_driver.h"
#include <stdio.h>
#include <string.h>

int repo_news_get_unread(db_t *db, int32_t player_id, db_res_t **out_res)
{
    char last_read_epoch[256];
    if (sql_ts_to_epoch_expr(db, "last_news_read_timestamp", last_read_epoch, sizeof(last_read_epoch)) != 0) return -1;

    /* SQL_VERBATIM: Q1 */
    char sql[512];
    snprintf(sql, sizeof(sql),
        "SELECT news_id, published_ts, news_category, article_text, author_id "
        "FROM news_feed WHERE published_ts > (SELECT %s FROM players WHERE player_id = {1}) "
        "ORDER BY published_ts DESC LIMIT 100;", last_read_epoch);

    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(player_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_news_get_recent(db_t *db, const char *epoch_expr, int32_t seconds_ago, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q2 */
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT news_id, published_ts, news_category, article_text, author_id "
             "FROM news_feed WHERE published_ts > (%s - {1}) "
             "ORDER BY published_ts DESC LIMIT 100;",
             epoch_expr);

    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(seconds_ago) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_news_update_last_read(db_t *db, const char *now_expr, int32_t player_id)
{
    /* SQL_VERBATIM: Q3 */
    /* SQL_VERBATIM: Q4 */
    char sql[512];
    snprintf(sql, sizeof(sql),
             "UPDATE players SET last_news_read_timestamp = %s WHERE player_id = {1};",
             now_expr);

    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_error_t err;
    if (db_exec(db, sql_converted, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &err)) {
        return 0;
    }
    return err.code;
}

int repo_news_post(db_t *db, const char *epoch_expr, const char *category, const char *body, int32_t author_id)
{
    /* SQL_VERBATIM: Q5 */
    char sql[512];
    snprintf(sql, sizeof(sql),
             "INSERT INTO news_feed (published_ts, news_category, article_text, author_id) VALUES (%s, {1}, {2}, {3});",
             epoch_expr);

    db_bind_t params[] = {
        db_bind_text(category),
        db_bind_text(body),
        db_bind_i64(author_id)
    };

    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_error_t err;
    if (db_exec(db, sql_converted, params, 3, &err)) {
        return 0;
    }
    return err.code;
}
