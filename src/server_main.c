#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sqlite3.h>
#include "config.h"		/* int load_config(void);   (1 = success, 0 = fail) */
#include "database.h"		/* int db_init(void); void db_close(void); */
#include "universe.h"		/* int universe_init(void); void universe_shutdown(void); */
#include "server_loop.h"	/* int server_loop(volatile sig_atomic_t *running); */
#include <pthread.h>		/* for pthread_mutex_t */

// If these exist elsewhere, keep them; otherwise these prototypes silence warnings
int universe_init (void);
void universe_shutdown (void);
int load_config (void);

static volatile sig_atomic_t running = 1;

/* forward decl: your bigbang entry point (adjust name/signature if different) */
static int bigbang (sqlite3 * db);	/* if your function is named differently, change this */

/*------------------- Signal helpers ---------------------------------*/

static void
handle_signal (int sig)
{
  (void) sig;
  running = 0;
}

static void
install_signal_handlers (void)
{
  struct sigaction sa = { 0 };
  sa.sa_handler = handle_signal;
  sigemptyset (&sa.sa_mask);
  /* No SA_RESTART: let blocking syscalls return EINTR */
  sigaction (SIGINT, &sa, NULL);
  sigaction (SIGTERM, &sa, NULL);
  sigaction (SIGHUP, &sa, NULL);
}

/*-------------------  Bigbang ---------------------------------*/

// Return first column of first row as int, or -1 on error
static int
get_scalar_int (const char *sql)
{
  sqlite3 *dbh = db_get_handle ();
  if (!dbh)
    return -1;
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL) != SQLITE_OK)
    return -1;
  int rc = sqlite3_step (st);
  int v = (rc == SQLITE_ROW) ? sqlite3_column_int (st, 0) : -1;
  sqlite3_finalize (st);
  return v;
}

// Decide if we need to run bigbang on this DB
static int
needs_bigbang (void)
{
  // Primary flag: PRAGMA user_version
  int uv = get_scalar_int ("PRAGMA user_version");
  if (uv > 0)
    return 0;			// already seeded

  // Belt-and-braces: look at contents in case user_version wasn't set
  int sectors = get_scalar_int ("SELECT COUNT(*) FROM sectors");
  int warps = get_scalar_int ("SELECT COUNT(*) FROM sector_warps");
  int ports = get_scalar_int ("SELECT COUNT(*) FROM ports");

  if (sectors <= 10)
    return 1;			// only the 10 Fedspace rows exist
  if (warps == 0)
    return 1;
  if (ports == 0)
    return 1;

  return 0;
}

// Run bigbang once; mark DB as seeded so we never do it again
static int
run_bigbang_if_needed (void)
{
  if (!needs_bigbang ())
    return 0;

  sqlite3 *dbh = db_get_handle ();
  if (!dbh)
    {
      fprintf (stderr, "BIGBANG: DB handle unavailable.\n");
      return -1;
    }

  fprintf (stderr, "BIGBANG: Universe appears empty — seeding now...\n");
  if (bigbang (dbh) != 0)
    {
      fprintf (stderr, "BIGBANG: Failed.\n");
      return -1;
    }

}


int
main (void)
{
/* Always open the global DB handle (creates schema/defaults if missing). */
  if (db_init () != 0)
    {
      fprintf (stderr, "Failed to init DB.\n");
      return EXIT_FAILURE;
    }

  /* Optional: keep a sanity check/log, but don't gate db_init() on it. */
  (void) load_config ();

  if (universe_init () != 0)
    {
      fprintf (stderr, "Failed to init universe.\n");
      db_close ();
      return EXIT_FAILURE;
    }

  if (run_bigbang_if_needed () != 0)
    {
      return EXIT_FAILURE;	// or your project’s error path
    }

  g_capabilities = json_object ();
  json_t *limits = json_object ();
  json_object_set_new (limits, "max_bulk", json_integer (100));
  json_object_set_new (limits, "max_page_size", json_integer (50));
  json_object_set_new (limits, "max_beacon_len", json_integer (256));
  json_object_set_new (g_capabilities, "limits", limits);

  json_t *features = json_object ();
  json_object_set_new (features, "auth", json_true ());
  json_object_set_new (features, "warp", json_true ());
  json_object_set_new (features, "sector.describe", json_true ());
  json_object_set_new (features, "trade.buy", json_true ());
  json_object_set_new(features, "server_autopilot", json_false());
  json_object_set_new (g_capabilities, "features", features);

  json_object_set_new (g_capabilities, "version",
		       json_string ("1.0.0-alpha"));


  install_signal_handlers ();

  int rc = server_loop (&running);

  universe_shutdown ();
  db_close ();

  // Clean up the global capabilities object when the server exits
  if (g_capabilities)
    {
      json_decref (g_capabilities);
    }

  return (rc == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}


/* Returns 1 if we can open twconfig.db and read at least one row from config; else 0. */
int
load_config (void)
{
  sqlite3 *db = NULL;
  sqlite3_stmt *stmt = NULL;
  int rc;

  rc = sqlite3_open ("twconfig.db", &db);
  if (rc != SQLITE_OK)
    {
      /* fprintf(stderr, "sqlite3_open: %s\n", sqlite3_errmsg(db)); */
      if (db)
	sqlite3_close (db);
      return 0;
    }

  rc =
    sqlite3_prepare_v2 (db, "SELECT 1 FROM config LIMIT 1;", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      /* fprintf(stderr, "sqlite3_prepare_v2: %s\n", sqlite3_errmsg(db)); */
      sqlite3_close (db);
      return 0;
    }

  rc = sqlite3_step (stmt);
  /* SQLITE_ROW means at least one row exists */
  int ok = (rc == SQLITE_ROW) ? 1 : 0;

  sqlite3_finalize (stmt);
  sqlite3_close (db);
  return ok;
}
