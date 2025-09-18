/* server_loop.c
 * Main server event loop implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>		/* for sleep() */
#include <signal.h>		/* for signal handling */
#include "server_loop.h"

static int running = 1;

/* Simple signal handler to stop server loop gracefully */
static void
handle_sigint (int sig)
{
  (void) sig;			/* unused */
  running = 0;
}

/* Main server loop */
int
server_loop (void)
{
  /* Register signal handlers */
  signal (SIGINT, handle_sigint);
  signal (SIGTERM, handle_sigint);

  printf ("Server loop starting...\n");

  while (running)
    {
      /* TODO: Replace this with your network polling / game tick logic */
      printf ("Server tick...\n");

      /* For now just sleep to simulate work */
      sleep (1);
    }

  printf ("Server loop exiting...\n");
  return 0;
}
