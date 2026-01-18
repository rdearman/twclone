#include <pthread.h>
#include <stdlib.h>
#include "game_db.h"
#include "server_config.h"
#include "server_log.h"
#include "db/db_api.h"
#include "errors.h"
#include <string.h>

// --- Per-Thread Connection Management ---

static pthread_key_t g_db_handle_key;
static pthread_once_t g_db_key_once = PTHREAD_ONCE_INIT;
static char g_main_conninfo[1024];
static db_backend_t g_main_backend;


static void
db_connection_destructor (void *handle)
{
  if (handle)
    {
      db_t *db = (db_t *) handle;


      LOGI ("Closing DB handle %p for thread.", (void *) db);
      db_close (db);
    }
}


static void
make_db_key (void)
{
  pthread_key_create (&g_db_handle_key, db_connection_destructor);
}


// GOAL A, B: Per-thread DB connections and fork-safety
int
game_db_init (void)
{
  pthread_once (&g_db_key_once, make_db_key);

#ifdef DB_BACKEND_PG
  g_main_backend = DB_BACKEND_POSTGRES;
  if (g_cfg.pg_conn_str)
    {
      strlcpy (g_main_conninfo, g_cfg.pg_conn_str, sizeof (g_main_conninfo));
      LOGI
	("game_db_init: Caching PostgreSQL conninfo for per-thread connections.");
    }
  else
    {
      LOGE
	("game_db_init: PostgreSQL backend enabled but no connection string is configured.");
      return -1;
    }
#endif
  return 0;
}


void
game_db_close (void)
{
  // This function is now primarily for server shutdown.
  // The pthread_key_delete could be called here if we had a mechanism
  // to ensure all threads are joined, but the destructor handles cleanup.
  LOGI ("Game DB layer shut down.");
}


db_t *
game_db_get_handle (void)
{
  pthread_once (&g_db_key_once, make_db_key);
  db_t *db = (db_t *) pthread_getspecific (g_db_handle_key);


  if (!db)
    {
      db_config_t db_cfg = { 0 };
      db_error_t err = { 0 };


      db_cfg.backend = g_main_backend;

#ifdef DB_BACKEND_PG
      db_cfg.pg_conninfo = g_main_conninfo;
      LOGI ("game_db_get_handle: Creating new PG connection for thread %lu.",
	    (unsigned long) pthread_self ());
#endif

      db = db_open (&db_cfg, &err);
      if (!db)
	{
	  LOGE
	    ("Failed to open new thread-local database connection (code: %d): %s",
	     err.code, err.message);
	  return NULL;
	}

      if (pthread_setspecific (g_db_handle_key, db) != 0)
	{
	  LOGE
	    ("Failed to set thread-specific database handle. Closing connection.");
	  db_close (db);
	  return NULL;
	}
    }
  return db;
}


void
game_db_after_fork_child (void)
{
  LOGI ("Child process after fork: cleaning up inherited DB handle state.");
  pthread_once (&g_db_key_once, make_db_key);
  db_t *db = (db_t *) pthread_getspecific (g_db_handle_key);


  if (db)
    {
      // We are the child. The parent still has its handle. We must close ours
      // to avoid sharing file descriptors, then set the TLS value to NULL so
      // this child process's main thread will create a new one on next access.
      db_connection_destructor (db);
      pthread_setspecific (g_db_handle_key, NULL);
    }
}
