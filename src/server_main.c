#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "database.h"
#include "server_loop.h"
#include "universe.h"
#include "config.h"

/* Global running flag */
static int running = 1;

/* Signal handler */
static void
handle_signal (int sig)
{
  (void) sig;
  running = 0;
}

int
main (int argc, char *argv[])
{
  /* Install signal handlers */
  signal (SIGINT, handle_signal);
  signal (SIGTERM, handle_signal);

  /* Initialise database */
  if (db_init () != 0)
    {
      fprintf (stderr, "Failed to initialise database.\n");
      return EXIT_FAILURE;
    }

  if (universe_init() != 0) {
    fprintf(stderr, "Failed to initialise universe.\n");
    db_close();
    return EXIT_FAILURE;
  }

  if (config_load () != 0)
    {
      fprintf (stderr, "No config found, inserting defaults.\n");
      if (initconfig () != 0)
	{
	  fprintf (stderr, "Failed to insert default config.\n");
	  db_close ();
	  return EXIT_FAILURE;
	}
    }

  /* Main server loop */
  while (running)
    {
      if (server_loop () != 0)
	{
	  fprintf (stderr, "server_loop() returned error, aborting.\n");
	  break;
	}
    }

  /* Shutdown subsystems */
  universe_shutdown ();
  db_close ();

  fprintf (stderr, "Server shutdown complete.\n");
  return EXIT_SUCCESS;
}
