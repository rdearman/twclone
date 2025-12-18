#include <sqlite3.h>
#include <jansson.h>
#include <string.h>
#include <strings.h>
#include "server_news.h"
#include "server_envelope.h"
#include "db_player_settings.h"
#include "database.h"
#include "server_log.h"
#include "server_players.h"	// For db_get_player_pref_*


// Helper to check if a category is in the filter string
static int
is_category_in_filter (const char *filter, const char *category)
{
  if (!filter || strcasecmp (filter, "all") == 0 || strlen (filter) == 0)
    {
      return 1;			// No filter means all categories are included
    }
  // For safety, wrap with commas to ensure we match whole words
  char padded_filter[512];


  snprintf (padded_filter, sizeof (padded_filter), ",%s,", filter);
  char padded_category[512];


  snprintf (padded_category, sizeof (padded_category), ",%s,", category);
  return strstr (padded_filter, padded_category) != NULL;
}


// Implementation for cmd_news_get_feed
int
cmd_news_get_feed (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SECTOR_NOT_FOUND, "Authentication required.");
      return 0;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_MISSING_FIELD,
			   "Database unavailable.");
      return 0;
    }
  // 1. Fetch player preferences
  int fetch_mode = db_get_player_pref_int (ctx->player_id, "news.fetch_mode",
					   7);	// Default to 7 days
  char category_filter[256];


  db_get_player_pref_string (ctx->player_id,
			     "news.category_filter",
			     "all",
			     category_filter, sizeof (category_filter));
  // 2. Build and execute the time-based part of the query
  char sql[512];
  sqlite3_stmt *stmt = NULL;
  int rc;


  if (fetch_mode == 0)
    {				// Unread
      snprintf (sql,
		sizeof (sql),
		"SELECT news_id, published_ts, news_category, article_text, source_ids FROM news_feed WHERE published_ts > (SELECT last_news_read_timestamp FROM players WHERE id = ?) ORDER BY published_ts DESC LIMIT 100;");
      rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
      if (rc == SQLITE_OK)
	{
	  sqlite3_bind_int (stmt, 1, ctx->player_id);
	}
    }
  else
    {				// Last N days
      snprintf (sql,
		sizeof (sql),
		"SELECT news_id, published_ts, news_category, article_text, source_ids FROM news_feed WHERE published_ts > (strftime('%%s','now') - ?) ORDER BY published_ts DESC LIMIT 100;");
      rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
      if (rc == SQLITE_OK)
	{
	  sqlite3_bind_int (stmt, 1, fetch_mode * 86400);	// N days in seconds
	}
    }
  if (rc != SQLITE_OK)
    {
      LOGE ("cmd_news_get_feed: Failed to prepare statement: %s",
	    sqlite3_errmsg (db));
      send_response_error (ctx, root, ERR_MISSING_FIELD,
			   "Database query error.");
      return 0;
    }
  // 3. Fetch results and filter by category
  json_t *articles = json_array ();


  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      const char *tmp_category = (const char *) sqlite3_column_text (stmt, 2);
      /* sqlite: column_text() pointer invalid after finalize/reset/step */
      char *category = tmp_category ? strdup (tmp_category) : NULL;


      if (is_category_in_filter (category_filter, category))
	{
	  json_t *article = json_object ();
	  const char *tmp_article =
	    (const char *) sqlite3_column_text (stmt, 3);
	  char *full_article_text = tmp_article ? strdup (tmp_article) : NULL;

	  const char *headline_start =
	    full_article_text ? strstr (full_article_text,
					"HEADLINE: ") : NULL;
	  const char *body_start =
	    full_article_text ? strstr (full_article_text,
					"\nBODY: ") : NULL;
	  char *extracted_headline = NULL;
	  char *extracted_body = NULL;


	  if (headline_start && body_start && body_start > headline_start)
	    {
	      headline_start += strlen ("HEADLINE: ");
	      // Extract headline
	      size_t headline_len = body_start - headline_start;


	      extracted_headline = (char *) malloc (headline_len + 1);
	      if (extracted_headline)
		{
		  strncpy (extracted_headline, headline_start, headline_len);
		  extracted_headline[headline_len] = '\0';
		}
	      // Extract body
	      body_start += strlen ("\nBODY: ");
	      extracted_body = strdup (body_start);
	    }
	  else
	    {
	      // Fallback if format is not as expected
	      extracted_headline = strdup ("Untitled News");
	      extracted_body =
		strdup (full_article_text ? full_article_text : "");
	    }
	  json_object_set_new (article, "id",
			       json_integer (sqlite3_column_int (stmt, 0)));
	  json_object_set_new (article, "timestamp",
			       json_integer (sqlite3_column_int64 (stmt, 1)));
	  json_object_set_new (article, "category",
			       json_string (category ? category : ""));
	  json_object_set_new (article, "scope", json_string (""));	// Hardcoded empty string
	  json_object_set_new (article, "headline",
			       json_string (extracted_headline ?
					    extracted_headline : ""));
	  json_object_set_new (article, "body",
			       json_string (extracted_body ? extracted_body :
					    ""));
	  const char *tmp_context =
	    (const char *) sqlite3_column_text (stmt, 4);
	  char *context_data_str = tmp_context ? strdup (tmp_context) : NULL;


	  if (context_data_str)
	    {
	      json_error_t error;
	      json_t *context_data = json_loads (context_data_str, 0, &error);


	      if (context_data)
		{
		  json_object_set_new (article, "context_data", context_data);
		}
	    }
	  json_array_append_new (articles, article);
	  // Free allocated strings
	  free (extracted_headline);
	  free (extracted_body);
	  free (full_article_text);
	  free (context_data_str);
	}
      free (category);
    }
  sqlite3_finalize (stmt);
  // 4. Send response
  json_t *data = json_object ();


  json_object_set_new (data, "articles", articles);
  send_response_ok_take (ctx, root, "news.feed", &data);
  return 0;
}


// Implementation for cmd_news_mark_feed_read
int
cmd_news_mark_feed_read (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SECTOR_NOT_FOUND, "Authentication required.");
      return 0;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_MISSING_FIELD,
			   "Database unavailable.");
      return 0;
    }
  sqlite3_stmt *stmt = NULL;
  const char *sql =
    "UPDATE players SET last_news_read_timestamp = strftime('%s','now') WHERE id = ?;";
  int rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("cmd_news_mark_feed_read: Failed to prepare statement: %s",
	    sqlite3_errmsg (db));
      send_response_error (ctx, root, ERR_MISSING_FIELD, "Database error.");
      return 0;
    }
  sqlite3_bind_int (stmt, 1, ctx->player_id);
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE
	("cmd_news_mark_feed_read: Failed to execute statement for player %d: %s",
	 ctx->player_id, sqlite3_errmsg (db));
      send_response_error (ctx,
			   root,
			   ERR_MISSING_FIELD,
			   "Database error during update.");
      return 0;
    }
  LOGI
    ("cmd_news_mark_feed_read: Updated last_news_read_timestamp for player %d.",
     ctx->player_id);
  send_response_ok_take (ctx, root, "news.marked_read", NULL);
  return 0;
}


int
news_post (const char *body, const char *category, int author_id)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      LOGE ("news_post: Database handle not available.");
      return -1;
    }
  sqlite3_stmt *stmt = NULL;
  const char *sql =
    "INSERT INTO news_feed (published_ts, news_category, article_text, author_id) VALUES (strftime('%s','now'), ?, ?, ?);";
  int rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("news_post: Failed to prepare statement: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_text (stmt, 1, category, -1, SQLITE_STATIC);
  sqlite3_bind_text (stmt, 2, body, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (stmt, 3, author_id);
  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE ("news_post: Failed to execute statement: %s",
	    sqlite3_errmsg (db));
    }
  sqlite3_finalize (stmt);
  return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}


int
cmd_news_read (client_ctx_t *ctx, json_t *root)
{
  // Minimal compat wrapper: alias to news.get_feed
  return cmd_news_get_feed (ctx, root);
}
