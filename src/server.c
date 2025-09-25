#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <jansson.h>
#include <ctype.h>
#include "player_interaction.h"
#include "hashtable.h"
#include "msgqueue.h"
#include "universe.h"
#include "serveractions.h"
#include "common.h"
#include "config.h"
#include "shipinfo.h"
#include "portinfo.h"
#include "planet.h"
#include "globals.h"

// Extern declarations for global variables used in this file
extern struct sp_shipinfo **shiptypes;
extern struct node **nodes;
extern struct port **ports;
extern struct planet **planets;
extern struct player **players;
extern struct ship **ships;
extern struct config *configdata;
extern planetClass **planetTypes;

// Prototypes for functions that will be implemented here
struct config *loadconfig (const char *file_path);
void initconfig (struct config *configdata);
struct sp_shipinfo **loadshiptypeinfo (const char *file_path);
struct port **loadallports (const char *file_path);
struct planet **loadplanets (const char *file_path);
struct node **loadnodes ();
planetClass **load_planetinfo (const char *file_path);
struct player **loadplayers (const char *file_path);
struct ship **loadships (const char *file_path);
void saveplayer (int pnumb, char *filename);
void saveship (int snumb, char *filename);
void init_shiptypeinfo (char *filename);
void saveshiptypeinfo (char *filename);
void save_planetinfo (char *filename);
int saveconfig (char *filename);
void saveallports (char *filename);
void saveplanets (char *filename);
void quit_handler (int);



/* void * */
/* makeplayerthreads (void *threadinfo) */
/* { */
/*   int newsock; */
/*   struct sockaddr_in clientname; */
/*   int size = sizeof (clientname); */
/*   pthread_t newthread; */
/*   struct connectinfo *newconnect; */

/*   newconnect = (struct connectinfo *) threadinfo; */

/*   listen (newconnect->sockid, 5); */
/*   while (1) */
/*     { */
/*       newsock = */
/* 	accept (newconnect->sockid, (struct sockaddr *) &clientname, &size); */
/*       if (newsock > 0) */
/* 	{ */
/* 	  threadcount++; */
/* 	  newconnect->sockid = newsock; */
/* 	  pthread_create (&newthread, NULL, handle_player, newconnect); */
/* 	} */
/*     } */
/* } */


/* void * */
/* handle_player (void *threadinfo) */
/* { */
/*   struct connectinfo *newconnect; */
/*   char *response_buffer = NULL; */
/*   size_t buffer_size = BUFF_SIZE; */

/*   newconnect = (struct connectinfo *) threadinfo; */

/*   int senderid = threadid++; */
/*   int n = 0; */

/*   response_buffer = (char *) malloc (buffer_size); */
/*   if (response_buffer == NULL) */
/*     { */
/*       perror ("handle_player: Failed to allocate response buffer"); */
/*       pthread_exit (NULL); */
/*     } */

/*   while (1) */
/*     { */
/*       char *json_data_string = getmsg (newconnect->msgidin, senderid, &n); */
/*       if (json_data_string == NULL) */
/* 	{ */
/* 	  fprintf (stderr, */
/* 		   "handle_player: Failed to get message from queue for senderid %d\n", */
/* 		   senderid); */
/* 	  // In a real application, you might want a more robust error handling */
/* 	  // or retry mechanism here. For this example, we'll just break. */
/* 	  break; */
/* 	} */

/*       json_t *json_data = json_loads (json_data_string, 0, NULL); */
/*       free (json_data_string);	// Free the string returned by getmsg */
/*       if (!json_data) */
/* 	{ */
/* 	  fprintf (stderr, */
/* 		   "handle_player: Failed to parse JSON message for senderid %d\n", */
/* 		   senderid); */
/* 	  continue;		// Skip to the next message */
/* 	} */

/*       // Check for quit command */
/*       json_t *command_obj = json_object_get (json_data, "command"); */
/*       if (json_is_string (command_obj) */
/* 	  && strcmp (json_string_value (command_obj), "ct_quit") == 0) */
/* 	{ */
/* 	  // Acknowledge the quit command */
/* 	  json_t *response_json = json_object (); */

/* 	  //////////////////////////////////////////////////////////////////// */
/* 	  json_object_set_new (response_json, "status", json_string ("OK")); */
/* 	  json_object_set_new (response_json, "response", */
/* 			       json_string ("Quitting.")); */
/* 	  ////////////////////////////////////////////////////////////////////// */
/* 	  sendmesg (newconnect->msgidout, response_json, 1, senderid); */
/* 	  json_decref (response_json); */
/* 	  json_decref (json_data); */
/* 	  break; */
/* 	} */

/*       processcommand (newconnect, json_data, response_buffer, buffer_size); */

/*       json_t *response_json = json_object (); */
/*       json_object_set_new (response_json, "response", */
/* 			   json_string (response_buffer)); */
/*       sendmesg (newconnect->msgidout, response_json, 1, senderid); */

/*       json_decref (response_json); */
/*       json_decref (json_data); */

/*       memset (response_buffer, 0, BUFF_SIZE); */
/*     } */

/*   free (response_buffer); */
/*   pthread_exit (NULL); */
/* } */


void *
process_commands (void *null)
{
  struct msgcommand data;
  char buffer[BUFF_SIZE];
  int senderid;

  while (1)
    {
      if (time (NULL) > next_process)
	{
	  next_process = time (NULL) + configdata->processinterval * 60;
	  doprocess ();
	}
      if (shutdown_flag)
	{
	  //send a message to main to start the shutdown
	  data.command = ct_quit;
	  sendmesg (msgidin, buffer, threadid, 0);
	  pthread_exit (NULL);
	}
    }
}


int
main (int argc, char **argv)
{
  struct sockaddr_in servername;
  int sock;
  pthread_t playerthread;
  pthread_t processthread;
  struct connectinfo *newconnect;
  struct msgcommand data;
  char buffer[BUFF_SIZE];
  int senderid;

  // Inside the main loop
  json_t *json_data = json_loads (buffer, 0, NULL);
  char response_buffer[BUFF_SIZE];	// Define a response buffer
  if (json_data)
    {
      processcommand (newconnect, json_data, response_buffer,
		      sizeof (response_buffer));
      // processcommand (json_data, response_buffer, sizeof (response_buffer));
      json_decref (json_data);	// Free the JSON object after use
    }


  printf ("Starting server...\n");
  fflush (stdout);

  srand (time (NULL));

  // Initialize global variables
  shiptypes = NULL;
  nodes = NULL;
  ports = NULL;
  planets = NULL;
  players = NULL;
  ships = NULL;
  configdata = NULL;
  planetTypes = NULL;

  // Load configuration and game data
  configdata = loadconfig ("config.data");
  if (!configdata)
    {
      configdata = (struct config *) malloc (sizeof (struct config));
      initconfig (configdata);
      printf
	("No config file found. Initializing with default settings...\n");
    }
  else
    {
      printf ("Config file loaded successfully.\n");
    }

  init_nodes (configdata->max_total_planets);
  init_hash_table (symbols, configdata->hash_length);

  shiptypes = loadshiptypeinfo ("shiptypes.data");
  if (!shiptypes)
    {
      shiptypes =
	(struct sp_shipinfo **) malloc (sizeof (struct sp_shipinfo *) *
					(configdata->ship_type_count + 1));
      init_shiptypeinfo ("shiptypes.data");
      printf
	("No ship types file found. Initializing with default settings...\n");
    }
  else
    {
      printf ("Ship types loaded successfully.\n");
    }

  ports = loadallports ("ports.data");
  if (!ports)
    {
      printf ("No ports file found. Initializing with default settings...\n");
    }
  else
    {
      printf ("Ports loaded successfully.\n");
    }

  planetTypes = load_planetinfo ("planettypes.data");
  if (!planetTypes)
    {
      printf
	("No planet types file found. Initializing with default settings...\n");
    }
  else
    {
      printf ("Planet types loaded successfully.\n");
    }

  planets = loadplanets ("planets.data");
  if (!planets)
    {
      printf
	("No planets file found. Initializing with default settings...\n");
    }
  else
    {
      printf ("Planets loaded successfully.\n");
    }

  players = loadplayers ("players.data");
  if (!players)
    {
      printf
	("No players file found. Initializing with default settings...\n");
    }
  else
    {
      printf ("Players loaded successfully.\n");
    }

  ships = loadships ("ships.data");
  if (!ships)
    {
      printf ("No ships file found. Initializing with default settings...\n");
    }
  else
    {
      printf ("Ships loaded successfully.\n");
    }

  //Initialize the message queues
  msgidin = init_msgqueue ();
  msgidout = init_msgqueue ();

  //set up the signal handler for quitting
  signal (SIGINT, quit_handler);

  //set up the socket to listen on
  sock = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (sock < 0)
    {
      perror ("Failure to create socket: ");
      exit (-1);
    }

  //bind the socket
  if (init_sockaddr (configdata->default_port, &servername) < 0)
    {
      perror ("init_sockaddr returned with a failure");
      exit (-1);
    }

  if (bind (sock, (struct sockaddr *) &servername, sizeof (servername)) < 0)
    {
      perror ("Failure to bind: ");
      exit (-1);
    }

  //create the threads for handling input from players and processing the game
  newconnect = (struct connectinfo *) malloc (sizeof (struct connectinfo));
  newconnect->sockid = sock;
  newconnect->msgidin = msgidin;
  newconnect->msgidout = msgidout;

  pthread_create (&playerthread, NULL, makeplayerthreads, newconnect);
  pthread_create (&processthread, NULL, process_commands, NULL);

  senderid = getdata (msgidin, &data, 0);
  while (data.command != ct_quit || senderid != threadid)	//Main game loop
    {
      // processcommand (buffer, &data);

      json_t *json_data = json_loads (buffer, 0, NULL);
      char response_buffer[BUFF_SIZE];
      if (json_data)
	{
	  processcommand (newconnect, json_data, response_buffer,
			  sizeof (response_buffer));
	  //      processcommand (json_data, response_buffer,                     sizeof (response_buffer));
	  json_decref (json_data);
	}
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
      exit (0);
    }

  if (fork () == 0)
    {
      sprintf (buffer, "%d", msgidout);
      fprintf (stderr, "Killing message queue with id %s...", buffer);
      if (execlp ("ipcrm", "ipcrm", "msg", buffer, NULL) < 0)
	{
	  perror ("Unable to exec: ");
	  printf ("Please run 'ipcrm msg %d'\n", msgidout);
	}
      exit (0);
    }

  printf ("\nServer shutdown complete.\n");
  exit (0);
}


void
quit_handler (int signal)
{
  shutdown_flag = 1;
}

// Data loading functions (placeholders)
struct config *
loadconfig (const char *file_path)
{
  FILE *fp = fopen (file_path, "rb");
  if (!fp)
    return NULL;
  struct config *data = malloc (sizeof (struct config));
  (void) fread (data, sizeof (struct config), 1, fp);
  fclose (fp);
  return data;
}

void
initconfig (struct config *configdata)
{
  configdata->turnsperday = 100;
  configdata->maxwarps = 6;
  configdata->startingcredits = 2000;
  configdata->startingfighters = 20;
  configdata->startingholds = 20;
  configdata->processinterval = 2;
  configdata->autosave = 10;
  configdata->max_players = 200;
  configdata->max_ships = 1024;
  configdata->max_ports = 500;
  configdata->max_planets = 200;
  configdata->max_total_planets = 200;
  configdata->max_safe_planets = 5;
  configdata->max_citadel_level = 7;
  configdata->number_of_planet_types = 8;
  configdata->max_ship_name_length = 40;
  configdata->ship_type_count = 15;
  configdata->hash_length = 200;
  configdata->default_port = 1234;
  configdata->default_nodes = 0;
  configdata->warps_per_sector = 6;
  configdata->buff_size = 5000;
  configdata->max_name_length = 25;
  configdata->planet_type_count = 8;
}

struct sp_shipinfo **
loadshiptypeinfo (const char *file_path)
{
  FILE *fp = fopen (file_path, "rb");
  if (!fp)
    return NULL;
  int count;
  (void) fread (&count, sizeof (int), 1, fp);
  struct sp_shipinfo **data =
    malloc (sizeof (struct sp_shipinfo *) * (count + 1));
  for (int i = 0; i < count; i++)
    {
      data[i] = malloc (sizeof (struct sp_shipinfo));
      (void) fread (data[i], sizeof (struct sp_shipinfo), 1, fp);
    }
  data[count] = NULL;
  fclose (fp);
  return data;
}

struct port **
loadallports (const char *file_path)
{
  FILE *fp = fopen (file_path, "rb");
  if (!fp)
    return NULL;
  int count;
  (void) fread (&count, sizeof (int), 1, fp);
  struct port **data = malloc (sizeof (struct port *) * (count + 1));
  for (int i = 0; i < count; i++)
    {
      data[i] = malloc (sizeof (struct port));
      (void) fread (data[i], sizeof (struct port), 1, fp);
    }
  data[count] = NULL;
  fclose (fp);
  return data;
}

struct planet **
loadplanets (const char *file_path)
{
  FILE *fp = fopen (file_path, "rb");
  if (!fp)
    return NULL;
  int count;
  (void) fread (&count, sizeof (int), 1, fp);
  struct planet **data = malloc (sizeof (struct planet *) * (count + 1));
  for (int i = 0; i < count; i++)
    {
      data[i] = malloc (sizeof (struct planet));
      (void) fread (data[i], sizeof (struct planet), 1, fp);
    }
  data[count] = NULL;
  fclose (fp);
  return data;
}

struct node **
loadnodes ()
{
  // This function's implementation is not available in the provided code.
  // It is a placeholder for now, as its implementation depends on your game's logic.
  return NULL;
}

planetClass **
load_planetinfo (const char *file_path)
{
  FILE *fp = fopen (file_path, "rb");
  if (!fp)
    return NULL;
  int count;
  (void) fread (&count, sizeof (int), 1, fp);
  planetClass **data = malloc (sizeof (planetClass *) * (count + 1));
  for (int i = 0; i < count; i++)
    {
      data[i] = malloc (sizeof (planetClass));
      (void) fread (data[i], sizeof (planetClass), 1, fp);
    }
  data[count] = NULL;
  fclose (fp);
  return data;
}

struct player **
loadplayers (const char *file_path)
{
  FILE *fp = fopen (file_path, "rb");
  if (!fp)
    return NULL;
  int count;
  (void) fread (&count, sizeof (int), 1, fp);
  struct player **data = malloc (sizeof (struct player *) * (count + 1));
  for (int i = 0; i < count; i++)
    {
      data[i] = malloc (sizeof (struct player));
      (void) fread (data[i], sizeof (struct player), 1, fp);
    }
  data[count] = NULL;
  fclose (fp);
  return data;
}

struct ship **
loadships (const char *file_path)
{
  FILE *fp = fopen (file_path, "rb");
  if (!fp)
    return NULL;
  int count;
  (void) fread (&count, sizeof (int), 1, fp);
  struct ship **data = malloc (sizeof (struct ship *) * (count + 1));
  for (int i = 0; i < count; i++)
    {
      data[i] = malloc (sizeof (struct ship));
      (void) fread (data[i], sizeof (struct ship), 1, fp);
    }
  data[count] = NULL;
  fclose (fp);
  return data;
}
