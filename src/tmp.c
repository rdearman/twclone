#define _REENTRANT
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include "common.h"
#include "universe.h"
#include "msgqueue.h"
#include "player_interaction.h"
#include "sysop_interaction.h"
#include "hashtable.h"
#include "planet.h"
#include "serveractions.h"
#include "shipinfo.h"
#include "config.h"
#include "maint.h"

struct sector **sectors;
int sectorcount;
struct list *symbols[HASH_LENGTH];
struct player **players;
struct ship **ships;
struct port **ports;
struct config *configdata;
struct sp_shipinfo **shiptypes;
struct node **nodes;

//time_t *timeptr;
time_t starttime;
int WARP_WAIT = 1;

int
main (int argc, char *argv[])
{
  int c;
  int sockid, port, msgidin, msgidout, senderid, rc;
  pthread_t threadid;
  struct connectinfo *threadinfo =
    (struct connectinfo *) malloc (sizeof (struct connectinfo));
  struct sockaddr_in serv_sockaddr;
  struct msgcommand data;
  char buffer[BUFF_SIZE];


  char *usageinfo =
    "Usage: server [options]\n    Options:-p < integer >\n    the port number the server will listen on (Default 1234) \n";
  port = DEFAULT_PORT;
  opterr = 0;

  while ((c = getopt (argc, argv, "p:")) != -1)
    {
      switch (c)
	{
	case 'p':
	  port = strtoul (optarg, NULL, 10);
	  break;
	case '?':
	  if (isprint (optopt))
	    fprintf (stderr, "Unknown option `-%c'.\n\n%s", optopt,
		     usageinfo);
	  else
	    fprintf (stderr,
		     "Unknown option character `\\x%x'.\n\n%s",
		     optopt, usageinfo);
	  return 1;
	default:
	  abort ();
	}
    }



  starttime = time (timeptr);

  init_hash_table (symbols, HASH_LENGTH);

  printf ("initializing configuration data from 'config.data'...");
  fflush (stdout);
  init_config ("./config.data");
  printf (" Done!\n");

  printf ("initializing planet type data from 'planettypes.data' ...");
  fflush (stdout);
  init_planetinfo ("./planettypes.data");
  printf (" Done!\n");

  printf ("initializing ship type data from 'shiptypes.data'...");
  init_shiptypeinfo ("./shiptypes.data");
  printf ("... Done!\n");

  printf ("initializing the universe from '%s'...", "universe.data");
  fflush (stdout);
  sectorcount = init_universe ("./universe.data", &sectors);
  printf (" Done!\n");

  printf ("Reading in planet information from 'planets.data'...\n");
  fflush (stdout);
  init_planets ("./planets.data");
  printf ("... Done!\n");

  printf ("Reading in ship information from 'ships.data'...");
  fflush (stdout);
  init_shipinfo ("./ships.data");
  printf (" Done!\n");

  printf ("Reading in player information from 'players.data'...");
  fflush (stdout);
  init_playerinfo ("./players.data");
  printf (" Done!\n");

  printf ("Reading in port information from 'ports.data'...");
  fflush (stdout);
  init_portinfo ("./ports.data");
  printf (" Done!\n");

  printf ("Configuring node information...");
  fflush (stdout);
  init_nodes (sectorcount);
  printf (" Done!\n");

  /*looks like maybe I shouldn't do this
     printf("Verify Universe...");
     fflush(stdout);
     if (verify_universe(sectors, sectorcount) < 0)
     {
     printf(" Failed, exiting!\n");
     exit(-1);
     }
     printf(" Done\n");
   */

  printf ("Initializing random number generator...");
  srand ((int) time (NULL));
  printf (" Done\n");

  printf ("Initializing message queues...");
  msgidin = init_msgqueue ();
  msgidout = init_msgqueue ();
  printf (" Done\n");
  printf ("Cleaning up any old message queues...");
  clean_msgqueues (msgidin, msgidout, "msgqueue.lock");
  printf (" Done\n");


  printf ("Creating sockets....");
  fflush (stdout);
  sockid = init_sockaddr (port, &serv_sockaddr);
  printf (" Listening on port %d!\n", port);

  threadinfo->sockid = sockid;
  threadinfo->msgidin = msgidin;
  threadinfo->msgidout = msgidout;
  if (pthread_create (&threadid, NULL, makeplayerthreads, (void *) threadinfo)
      != 0)
    {
      perror ("Unable to Create Listening Thread");
      exit (-1);
    }
  printf ("Accepting connections!\n");

  printf ("Initializing background maintenance...");
  fflush (stdout);
  threadinfo = (struct connectinfo *) malloc (sizeof (struct connectinfo));
  threadinfo->msgidin = msgidin;
  threadinfo->msgidout = msgidout;
  if (pthread_create (&threadid, NULL, background_maint, (void *) threadinfo)
      != 0)
    {
      perror ("Unable to Create Backgroud Thread");
      exit (-1);
    }
  printf ("Done!\n");

  threadinfo = (struct connectinfo *) malloc (sizeof (struct connectinfo));
  threadinfo->msgidin = msgidin;
  threadinfo->msgidout = msgidout;
  if (pthread_create (&threadid, NULL, getsysopcommands, (void *) threadinfo)
      != 0)
    {
      perror ("Unable to Create Sysop Thread");
      exit (-1);
    }
  printf ("Accepting Sysop commands\n");

  //process the commands from the threads
  senderid = getdata (msgidin, &data, 0);
  while (data.command != ct_quit || senderid != threadid)	//Main game loop
    {
      processcommand (buffer, &data);
      sendmesg (msgidout, buffer, senderid);
      senderid = getdata (msgidin, &data, 0);
    }
  printf ("\nSaving all ports ...");
  fflush (stdout);
  saveallports ("ports.data");
  printf ("Done!");
  printf ("\nSaving planets ...");
  fflush (stdout);
  saveplanets ("planets.data");
  printf ("Done!");
  printf ("\nSaving ship types ...");
  fflush (stdout);
  saveshiptypeinfo ("shiptypes.data");
  printf ("Done!");
  printf ("\nSaving planet types ...");
  fflush (stdout);
  save_planetinfo ("planettypes.data");
  printf ("Done!");
  printf ("\nSaving configuration data ...");
  fflush (stdout);
  saveconfig ("config.data");
  printf ("Done!");
  fflush (stdout);

  printf ("\nPlease run 'rm msgqueue.lock'\n");
  //when we're done, clean up the msg queues
  if (fork () == 0)
    {
      sprintf (buffer, "%d", msgidin);
      fprintf (stderr, "Killing message queue with id %s...", buffer);
      if (execlp ("ipcrm", "ipcrm", "msg", buffer, NULL) < 0)
	{
	  perror ("Unable to exec: ");
	  printf ("Please run 'ipcrm msg %d'\n", msgidin);
	}
    }
  else
    {
      sprintf (buffer, "%d", msgidout);
      fprintf (stderr, "Killing message queue with id %s...", buffer);
      if (execlp ("ipcrm", "ipcrm", "msg", buffer, NULL) < 0)
	{
	  perror ("Unable to exec: ");
	  printf ("Please run 'ipcrm msg %d'\n", msgidout);
	}
    }
  return 0;
}
