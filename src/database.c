#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "database.h"

static sqlite3 *db_handle = NULL;
const char *DEFAULT_DB_NAME = "twconfig.db";
/* Number of tables */
static const size_t create_table_count =
  sizeof (create_table_sql) / sizeof (create_table_sql[0]);
/* Number of default inserts */
static const size_t insert_default_count =
  sizeof (insert_default_sql) / sizeof (insert_default_sql[0]);

sqlite3 *
db_get_handle (void)
{
  return db_handle;
}

int
db_init (void)
{
  /* Step 1: open or create DB file */
  if (sqlite3_open (DEFAULT_DB_NAME, &db_handle) != SQLITE_OK)
    {
      fprintf (stderr, "DB init error: %s\n", sqlite3_errmsg (db_handle));
      return -1;
    }

  /* Step 2: check if config table exists */
  const char *sql =
    "SELECT name FROM sqlite_master WHERE type='table' AND name='config';";
  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2 (db_handle, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "DB check error: %s\n", sqlite3_errmsg (db_handle));
      sqlite3_close (db_handle);
      db_handle = NULL;
      return -1;
    }

  rc = sqlite3_step (stmt);
  int table_exists = (rc == SQLITE_ROW);	/* row means table found */
  sqlite3_finalize (stmt);

  /* Step 3: if no config table, create schema + defaults */
  if (!table_exists)
    {
      fprintf (stderr,
	       "No schema detected – creating tables and inserting defaults...\n");

      if (db_create_tables () != 0)
	{
	  fprintf (stderr, "Failed to create tables\n");
	  return -1;
	}

      if (db_insert_defaults () != 0)
	{
	  fprintf (stderr, "Failed to insert default data\n");
	  return -1;
	}
    }

  return 0;
}



int
db_create_tables (void)
{
  char *errmsg = NULL;

  for (size_t i = 0; i < create_table_count; i++)
    {
      if (sqlite3_exec (db_handle, create_table_sql[i], NULL, NULL, &errmsg)
	  != SQLITE_OK)
	{
	  fprintf (stderr, "DB create_tables error (%zu): %s\n", i, errmsg);
	  sqlite3_free (errmsg);
	  return -1;
	}
    }

  return 0;
}



int
db_insert_defaults (void)
{
  char *errmsg = NULL;

  for (size_t i = 0; i < insert_default_count; i++)
    {
      if (sqlite3_exec (db_handle, insert_default_sql[i], NULL, NULL, &errmsg)
	  != SQLITE_OK)
	{
	  fprintf (stderr, "DB insert_defaults error (%zu): %s\n", i, errmsg);
	  sqlite3_free (errmsg);
	  return -1;
	}
    }

  return 0;
}



/* Close database */
void
db_close (void)
{
  if (db_handle)
    {
      sqlite3_close (db_handle);
      db_handle = NULL;
    }
}

////////////////////////////////////////////

/* Create row in table from JSON */
int
db_create (const char *table, json_t *row)
{
  /* TODO: Build INSERT SQL dynamically based on JSON keys/values */
  fprintf (stderr, "db_create(%s, row) called (not implemented)\n", table);
  return 0;
}

/* Read row by id into JSON */
json_t *
db_read (const char *table, int id)
{
  /* TODO: Prepare SELECT ... WHERE id=? and return json_t * */
  fprintf (stderr, "db_read(%s, %d) called (not implemented)\n", table, id);
  return NULL;
}

/* Update row by id with new JSON */
int
db_update (const char *table, int id, json_t *row)
{
  /* TODO: Build UPDATE SQL dynamically */
  fprintf (stderr, "db_update(%s, %d, row) called (not implemented)\n", table,
	   id);
  return 0;
}

/* Delete row by id */
int
db_delete (const char *table, int id)
{
  /* TODO: Prepare DELETE ... WHERE id=? */
  fprintf (stderr, "db_delete(%s, %d) called (not implemented)\n", table, id);
  return 0;
}
