#include <stdio.h>
#include <sqlite3.h>
#include "database.h"
#include "server_bigbang.h"

static void
dump_sectors (void)
{
  sqlite3 *handle = db_get_handle ();
  const char *sql = "SELECT id, name FROM sectors;";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2 (handle, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "dump_sectors prepare error: %s\n",
	       sqlite3_errmsg (handle));
      return;
    }

  printf ("Sectors:\n");
  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      int id = sqlite3_column_int (stmt, 0);
      const unsigned char *name = sqlite3_column_text (stmt, 1);
      printf ("  %d: %s\n", id, name);
    }

  sqlite3_finalize (stmt);
}

static void
dump_warps (void)
{
  sqlite3 *handle = db_get_handle ();
  const char *sql = "SELECT from_sector, to_sector FROM sector_warps;";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2 (handle, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "dump_warps prepare error: %s\n",
	       sqlite3_errmsg (handle));
      return;
    }

  printf ("Warps:\n");
  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      int from = sqlite3_column_int (stmt, 0);
      int to = sqlite3_column_int (stmt, 1);
      printf ("  %d -> %d\n", from, to);
    }

  sqlite3_finalize (stmt);
}

int
main (void)
{
  if (db_init () != 0)
    {
      fprintf (stderr, "DB init failed\n");
      return 1;
    }

  sqlite3 *handle = db_get_handle ();

  /* clear tables before bigbang */
  // sqlite3_exec(handle, "DELETE FROM sector_warps;", NULL, NULL, NULL);
  // sqlite3_exec(handle, "DELETE FROM sectors;", NULL, NULL, NULL);

  if (bigbang () != 0)
    {
      fprintf (stderr, "Bigbang failed\n");
      db_close ();
      return 1;
    }

  //    dump_sectors();
  //    dump_warps();

  db_close ();
  return 0;
}
