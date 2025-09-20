#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "config.h"		/* int load_config(void);   (1 = success, 0 = fail) */
#include "database.h"		/* int db_init(void); void db_close(void); */
#include "universe.h"		/* int universe_init(void); void universe_shutdown(void); */
#include "server_loop.h"	/* int server_loop(volatile sig_atomic_t *running); */
#include <sqlite3.h>

int load_config (void);

static volatile sig_atomic_t running = 1;

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

  install_signal_handlers ();

  int rc = server_loop (&running);

  universe_shutdown ();
  db_close ();

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
