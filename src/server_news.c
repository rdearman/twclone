#include "db/repo/repo_news.h"
/* src/server_news.c */
#include <jansson.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

/* local includes */
#include "server_news.h"
#include "server_envelope.h"
#include "repo_player_settings.h"
#include "db/repo/repo_database.h"
#include "game_db.h"
#include "db/sql_driver.h"
#include "server_log.h"
#include "server_players.h"
#include "db/db_api.h"
#include "db/sql_driver.h"


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
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_MISSING_FIELD,
			   "Database unavailable.");
      return 0;
    }

  // 1. Fetch player preferences
  int fetch_mode = db_get_player_pref_int (db,
					   ctx->player_id,
					   "news.fetch_mode",
					   7);
  char category_filter[256];


  db_get_player_pref_string (db, ctx->player_id, "news.category_filter",
			     "all", category_filter,
			     sizeof (category_filter));

  // 2. Build and execute the time-based part of the query
  db_res_t *res = NULL;
  db_error_t err;


  if (fetch_mode == 0)
    {				// Unread
      if (repo_news_get_unread (db, ctx->player_id, &res) != 0)
	{
	  goto sql_err;
	}
    }
  else
    {				// Last N days
      const char *epoch_expr = sql_epoch_now (db);
      if (!epoch_expr)
	{
	  LOGE ("news.feed: unsupported database backend");
	  goto sql_err;
	}

      if (repo_news_get_recent (db, epoch_expr, fetch_mode * 86400, &res) !=
	  0)
	{
	  goto sql_err;
	}
    }

  // 3. Fetch results and filter by category
  json_t *articles = json_array ();


  while (db_res_step (res, &err))
    {
      const char *category = db_res_col_text (res,
					      2,
					      &err);


      if (is_category_in_filter (category_filter, category))
	{
	  json_t *article = json_object ();
	  const char *full_article_text = db_res_col_text (res, 3, &err);

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
	      size_t headline_len = body_start - headline_start;


	      extracted_headline = (char *) malloc (headline_len + 1);
	      if (extracted_headline)
		{
		  strncpy (extracted_headline, headline_start, headline_len);
		  extracted_headline[headline_len] = '\0';
		}
	      body_start += strlen ("\nBODY: ");
	      extracted_body = strdup (body_start);
	    }
	  else
	    {
	      extracted_headline = strdup ("Untitled News");
	      extracted_body =
		strdup (full_article_text ? full_article_text : "");
	    }

	  json_object_set_new (article, "id",
			       json_integer (db_res_col_i32 (res, 0, &err)));
	  json_object_set_new (article, "timestamp",
			       json_integer (db_res_col_i64 (res, 1, &err)));
	  json_object_set_new (article, "category",
			       json_string (category ? category : ""));
	  json_object_set_new (article, "scope", json_string (""));
	  json_object_set_new (article, "headline",
			       json_string (extracted_headline ?
					    extracted_headline : ""));
	  json_object_set_new (article,
			       "body",
			       json_string (extracted_body ? extracted_body :
					    ""));

	  const char *context_data_str = db_res_col_text (res, 4, &err);


	  if (context_data_str && context_data_str[0] == '{')
	    {
	      json_error_t jerr;
	      json_t *context_data = json_loads (context_data_str, 0, &jerr);


	      if (context_data)
		{
		  json_object_set_new (article, "context_data", context_data);
		}
	    }
	  json_array_append_new (articles, article);
	  free (extracted_headline);
	  free (extracted_body);
	}
    }
  db_res_finalize (res);

  // 4. Update read status (Auto-Mark Read behavior requested by audit)
  const char *now_expr = sql_now_timestamptz (db);
  if (!now_expr)
    {
      LOGE ("cmd_news_feed: unsupported database backend");
      goto sql_err;
    }

  repo_news_update_last_read (db, now_expr, ctx->player_id);

  // 5. Send response
  json_t *payload = json_object ();


  json_object_set_new (payload, "articles", articles);
  send_response_ok_take (ctx, root, "news.feed", &payload);
  return 0;

sql_err:
  LOGE ("cmd_news_get_feed: DB error: %s", err.message);
  send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
		       "Database query error.");
  return 0;
}


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
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_MISSING_FIELD,
			   "Database unavailable.");
      return 0;
    }

  const char *now_expr = sql_now_timestamptz (db);
  if (!now_expr)
    {
      LOGE ("cmd_news_mark_feed_read: unsupported database backend");
      send_response_error (ctx, root, ERR_MISSING_FIELD, "Database error.");
      return 0;
    }

  if (repo_news_update_last_read (db, now_expr, ctx->player_id) != 0)
    {
      LOGE ("cmd_news_mark_feed_read: Failed for player %d", ctx->player_id);
      send_response_error (ctx, root, ERR_MISSING_FIELD, "Database error.");
      return 0;
    }

  LOGI
    ("cmd_news_mark_feed_read: Updated last_news_read_timestamp for player %d.",
     ctx->player_id);
  send_response_ok_take (ctx, root, "news.marked_read", NULL);
  return 0;
}


int
cmd_news_read (client_ctx_t *ctx, json_t *root)
{
  return cmd_news_get_feed (ctx, root);
}


int
news_post (const char *body, const char *cat, int aid)
{


  db_t *db = game_db_get_handle ();


  if (!db)


    {


      LOGE ("news_post: Database handle not available.");


      return -1;


    }


  const char *epoch_expr = sql_epoch_now (db);


  if (!epoch_expr)


    {


      LOGE ("news_post: unsupported database backend");


      return -1;


    }





  if (repo_news_post (db, epoch_expr, cat, body, aid) != 0)


    {


      LOGE ("news_post: Failed");


      return -1;


    }


  return 0;


}
