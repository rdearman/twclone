#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <jansson.h>		// Include jansson library for JSON handling
#include "parse.h"
#include "msgqueue.h"
#include "common.h"
#include "player_interaction.h"
#include "universe.h"

/*
  makeplayerthreads

  This thread sits and waits for network connections, when it gets them,
  it spews forth another thread to handle them
 */

void *
makeplayerthreads (void *threadinfo)
{
  int sockid = ((struct connectinfo *) threadinfo)->sockid,
    msgidin = ((struct connectinfo *) threadinfo)->msgidin, sockaid,
    msgidout = ((struct connectinfo *) threadinfo)->msgidout;

  pthread_t threadid;

  free (threadinfo);
  do
    {
      threadinfo =
	(struct connectinfo *) malloc (sizeof (struct connectinfo));
      sockaid = acceptnewconnection (sockid);

      //putting the info in the special struct for the thread
      ((struct connectinfo *) threadinfo)->sockid = sockaid;
      ((struct connectinfo *) threadinfo)->msgidin = msgidin;
      ((struct connectinfo *) threadinfo)->msgidout = msgidout;

      //make the threads, passing them the stuff to connect to the client
      if (pthread_create (&threadid, NULL, handle_player, (void *) threadinfo)
	  != 0)
	{
	  perror ("Unable to Create Thread");
	  exit (-1);
	}

    }
  while (1);			//we want this to last forever

  close (sockid);

  return NULL;
}


/*
  handle_player

  This is the function that the thread runs.  It handles all of the communication
  for the players.
*/

void *
handle_player (void *threadinfo)
{
  int sector, sockid = ((struct connectinfo *) threadinfo)->sockid,
    msgidout = ((struct connectinfo *) threadinfo)->msgidout,
    msgidin = ((struct connectinfo *) threadinfo)->msgidin,
    commandgood, loggedin;
  char inbuffer[BUFF_SIZE], outbuffer[BUFF_SIZE],
    name[MAX_NAME_LENGTH + 1], passwd[MAX_NAME_LENGTH + 1], temp[BUFF_SIZE];
  json_t *data_json = NULL;
  json_t *response_json = NULL;
  long senderid = pthread_self ();

  free (threadinfo);

  printf ("Thread %ld: Created\n", senderid);
  loggedin = 0;

  do
    {
      commandgood = 0;
      data_json = json_object ();

      if (recvinfo (sockid, inbuffer) == -1)
	{
	  fprintf (stderr, "Thread %ld: Exiting!\n", senderid);
	  fflush (stderr);
	  pthread_exit (NULL);
	}

      json_object_set_new (data_json, "sockid", json_integer (sockid));
      json_object_set_new (data_json, "threadid", json_integer (senderid));
      json_object_set_new (data_json, "name", json_string (name));

      //fprintf(stderr, "handle_player: I got '%s' as the messagem and loggedin = %d\n", 
      //inbuffer, loggedin);

      //parse stuff from client, should be expanded, modularized
      if (strncmp (inbuffer, "DESCRIPTION", strlen ("DESCRIPTION")) == 0
	  && loggedin)
	{
	  printf ("Thread %ld: Player Querried\n", senderid);
	  json_object_set_new (data_json, "command",
			       json_string ("ct_describe"));
	  commandgood = 1;
	}
      else if (((strncmp (inbuffer, "USER", strlen ("USER")) == 0) ||
		(strncmp (inbuffer, "NEW", strlen ("NEW")) == 0))
	       && !loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);

	  json_object_set_new (data_json, "command",
			       json_string ((strlen (temp) ==
					     4) ? "ct_login" :
					    "ct_newplayer"));
	  popstring (inbuffer, name, ":", BUFF_SIZE);
	  popstring (inbuffer, passwd, ":", BUFF_SIZE);
	  popstring (inbuffer, temp, ":", BUFF_SIZE);

	  fprintf (stderr,
		   "Thread %ld: Player '%s' trying to login with passwd '%s'\n",
		   senderid, name, passwd);
	  json_object_set_new (data_json, "name", json_string (name));
	  json_object_set_new (data_json, "passwd", json_string (passwd));
	  json_object_set_new (data_json, "buffer", json_string (temp));

	  commandgood = 2;
	}
      else if (strncmp (inbuffer, "PLAYERINFO", strlen ("PLAYERINFO")) == 0
	       && loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  json_object_set_new (data_json, "to",
			       json_integer (popint (inbuffer, ":")));
	  json_object_set_new (data_json, "command",
			       json_string ("ct_playerinfo"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "UPDATE", strlen ("UPDATE")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_update"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "ONLINE", strlen ("ONLINE")) == 0)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_online"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "FEDCOMM", strlen ("FEDCOMM")) == 0
	       && loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  popstring (inbuffer, temp, ":", BUFF_SIZE);
	  json_object_set_new (data_json, "buffer", json_string (temp));
	  json_object_set_new (data_json, "command",
			       json_string ("ct_fedcomm"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "STARDOCK", strlen ("STARDOCK")) == 0
	       && loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  popstring (inbuffer, temp, ":", BUFF_SIZE);
	  if (strncmp (temp, "BUYSHIP", strlen ("BUYSHIP")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_buyship"));
	  else if (strncmp (temp, "SELLSHIP", strlen ("SELLSHIP")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_sellship"));
	  else if (strncmp (temp, "PRICESHIP", strlen ("PRICESHIP")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_priceship"));
	  else if (strncmp (temp, "LISTSHIPS", strlen ("PRICESHIP")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_listships"));
	  else if (strncmp (temp, "DEPOSIT", strlen ("DEPOSIT")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_deposit"));
	  else if (strncmp (temp, "WITHDRAW", strlen ("WITHDRAW")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_withdraw"));
	  else if (strncmp (temp, "BALANCE", strlen ("BALANCE")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_balance"));
	  else if (strncmp (temp, "BUYHARDWARE", strlen ("BUYHARDWARE")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_buyhardware"));
	  json_object_set_new (data_json, "buffer", json_string (inbuffer));
	  json_object_set_new (data_json, "command",
			       json_string ("ct_stardock"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "NODE", strlen ("NODE")) == 0 && loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  popstring (inbuffer, temp, ":", BUFF_SIZE);
	  if (strncmp (temp, "BUYSHIP", strlen ("BUYSHIP")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_buyship"));
	  else if (strncmp (temp, "SELLSHIP", strlen ("SELLSHIP")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_sellship"));
	  else if (strncmp (temp, "PRICESHIP", strlen ("PRICESHIP")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_priceship"));
	  else if (strncmp (temp, "LISTSHIPS", strlen ("PRICESHIP")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_listships"));
	  else if (strncmp (temp, "DEPOSIT", strlen ("DEPOSIT")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_deposit"));
	  else if (strncmp (temp, "WITHDRAW", strlen ("WITHDRAW")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_withdraw"));
	  else if (strncmp (temp, "BALANCE", strlen ("BALANCE")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_balance"));
	  else if (strncmp (temp, "BUYHARDWARE", strlen ("BUYHARDWARE")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_buyhardware"));
	  else if (strncmp (temp, "LISTNODES", strlen ("LISTNODES")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("pn_listnodes"));
	  else if (strncmp (temp, "TRAVEL", strlen ("TRAVEL")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("pn_travel"));
	  json_object_set_new (data_json, "buffer", json_string (inbuffer));
	  json_object_set_new (data_json, "command", json_string ("ct_node"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LAND", strlen ("LAND")) == 0 && loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  json_object_set_new (data_json, "to",
			       json_integer (popint (inbuffer, ":")));
	  json_object_set_new (data_json, "command", json_string ("ct_land"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "ONPLANET", strlen ("PLANET")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_onplanet"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "PLANET", strlen ("PLANET")) == 0
	       && loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  popstring (inbuffer, temp, ":", BUFF_SIZE);
	  if (strncmp (temp, "DISPLAY", strlen ("DISPLAY")) == 0)
	    json_object_set_new (data_json, "plcommand",
				 json_string ("pl_display"));
	  else if (strncmp (temp, "OWNERSHIP", strlen ("OWNERSHIP")) == 0)
	    json_object_set_new (data_json, "plcommand",
				 json_string ("pl_ownership"));
	  else if (strncmp (temp, "DESTROY", strlen ("DESTROY")) == 0)
	    json_object_set_new (data_json, "plcommand",
				 json_string ("pl_destroy"));
	  else if (strncmp (temp, "TAKE", strlen ("TAKE")) == 0)
	    json_object_set_new (data_json, "plcommand",
				 json_string ("pl_take"));
	  else if (strncmp (temp, "LEAVE", strlen ("LEAVE")) == 0)
	    json_object_set_new (data_json, "plcommand",
				 json_string ("pl_leave"));
	  else if (strncmp (temp, "CITADEL", strlen ("CITADEL")) == 0)
	    json_object_set_new (data_json, "plcommand",
				 json_string ("pl_citadel"));
	  else if (strncmp (temp, "REST", strlen ("REST")) == 0)
	    json_object_set_new (data_json, "plcommand",
				 json_string ("pl_rest"));
	  else if (strncmp (temp, "MRL", strlen ("MRL")) == 0)
	    json_object_set_new (data_json, "plcommand",
				 json_string ("pl_militarylvl"));
	  else if (strncmp (temp, "QCANNON", strlen ("QCANNON")) == 0)
	    json_object_set_new (data_json, "plcommand",
				 json_string ("pl_qcannon"));
	  else if (strncmp (temp, "EVICT", strlen ("EVICT")) == 0)
	    json_object_set_new (data_json, "plcommand",
				 json_string ("pl_evict"));
	  else if (strncmp (temp, "SWAP", strlen ("SWAP")) == 0)
	    json_object_set_new (data_json, "plcommand",
				 json_string ("pl_swap"));
	  else if (strncmp (temp, "UPGRADE", strlen ("UPGRADE")) == 0)
	    json_object_set_new (data_json, "plcommand",
				 json_string ("pl_upgrade"));
	  else if (strncmp (temp, "CQUIT", strlen ("CQUIT")) == 0)
	    json_object_set_new (data_json, "plcommand",
				 json_string ("pl_cquit"));
	  else if (strncmp (temp, "QUIT", strlen ("QUIT")) == 0)
	    json_object_set_new (data_json, "plcommand",
				 json_string ("pl_quit"));
	  json_object_set_new (data_json, "buffer", json_string (inbuffer));
	  json_object_set_new (data_json, "command",
			       json_string ("ct_planet"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "SCAN", strlen ("SCAN")) == 0 && loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  json_object_set_new (data_json, "command", json_string ("ct_scan"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "BEACON", strlen ("BEACON")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_beacon"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "TOW", strlen ("TOW")) == 0 && loggedin)
	{
	  json_object_set_new (data_json, "command", json_string ("ct_tow"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTNAV", strlen ("LISTNAV")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_listnav"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "TRANSPORTER", strlen ("TRANSPORTER")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_transporter"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "GENESIS", strlen ("GENESIS")) == 0
	       && loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  json_object_set_new (data_json, "buffer", json_string (inbuffer));
	  json_object_set_new (data_json, "command",
			       json_string ("ct_genesis"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "JETTISON", strlen ("JETTISON")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_jettison"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "INTERDICT", strlen ("INTERDICT")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_interdict"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "ATTACK", strlen ("ATTACK")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_attack"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "ETHERPROBE", strlen ("ETHERPROBE")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_etherprobe"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "FIGHTERS", strlen ("FIGHTERS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_fighters"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTFIGHTERS", strlen ("LISTFIGHTERS")) ==
	       0 && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_listfighters"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "MINES", strlen ("MINES")) == 0 && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_mines"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTMINES", strlen ("LISTMINES")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_listmines"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "PORTUPGRADE", strlen ("PORTUPGRADE")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_portconstruction"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "SETNAVS", strlen ("SETNAVS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_setnavs"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "STATUS", strlen ("STATUS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_gamestatus"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "HAIL", strlen ("HAIL")) == 0 && loggedin)
	{
	  json_object_set_new (data_json, "command", json_string ("ct_hail"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "SUBSPACE", strlen ("SUBSPACE")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_subspace"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTWARPS", strlen ("LISTWARPS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_listwarps"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "CIM", strlen ("CIM")) == 0 && loggedin)
	{
	  json_object_set_new (data_json, "command", json_string ("ct_cim"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "AVOID", strlen ("AVOID")) == 0 && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_avoid"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTAVOIDS", strlen ("LISTAVOIDS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_listavoids"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "SELFDESTRUCT", strlen ("SELFDESTRUCT")) ==
	       0 && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_selfdestruct"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTSETTINGS", strlen ("LISTSETTINGS")) ==
	       0 && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_listsettings"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "SETTINGS", strlen ("SETTINGS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_updatesettings"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "ANNOUNCE", strlen ("ANNOUNCE")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_dailyannouncement"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "PHOTON", strlen ("PHOTON")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_firephoton"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "READMAIL", strlen ("READMAIL")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_readmail"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "SENDMAIL", strlen ("SENDMAIL")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_sendmail"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "TIME", strlen ("TIME")) == 0 && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_shiptime"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "DISRUPTOR", strlen ("DISRUPTOR")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_usedisruptor"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "DAILYLOG", strlen ("DAILYLOG")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_dailylog"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "ALIENRANKS", strlen ("ALIENRANKS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_alienranks"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTPLAYERS", strlen ("LISTPLAYERS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_listplayers"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTPLANETS", strlen ("LISTPLANETS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_listplanets"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTSHIPS", strlen ("LISTSHIPS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_listships"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTCORPS", strlen ("LISTCORPS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_listcorps"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "JOINCORP", strlen ("JOINCORP")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_joincorp"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "MAKECORP", strlen ("MAKECORP")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_makecorp"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "CREDTRANS", strlen ("CREDTRANS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_credittransfer"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "FIGTRANS", strlen ("FIGTRANS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_fightertransfer"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "MINETRANS", strlen ("MINETRANS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_minetransfer"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "SHIELDTRANS", strlen ("SHIELDTRANS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_shieldtransfer"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "CORPQUIT", strlen ("CORPQUIT")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_quitcorp"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "CORPPLANETS", strlen ("CORPPLANETS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_listcorpplanets"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "ASSETS", strlen ("ASSETS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_showassets"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "CORPMEMO", strlen ("CORPMEMO")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_corpmemo"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "DROPMEMBER", strlen ("DROPMEMBER")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_dropmember"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "CORPPASS", strlen ("CORPPASS")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_corppassword"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "SHIPINFO", strlen ("SHIPINFO")) == 0
	       && loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  json_object_set_new (data_json, "to",
			       json_integer (popint (inbuffer, ":")));
	  json_object_set_new (data_json, "command",
			       json_string ("ct_shipinfo"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "QUIT", strlen ("QUIT")) == 0 && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_logout"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "PORTINFO", strlen ("PORTINFO")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_portinfo"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "PORT", strlen ("PORT")) == 0 && loggedin)
	{
	  printf ("Thread %ld: Player attempting to port\n", senderid);
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  popstring (inbuffer, temp, ":", BUFF_SIZE);
	  if (strncmp (temp, "TRADE", strlen ("TRADE")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_trade"));
	  else if (strncmp (temp, "LAND", strlen ("LAND")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_land"));
	  else if (strncmp (temp, "NEGOTIATE", strlen ("NEGOTIATE")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_trade"));
	  else if (strncmp (temp, "UPGRADE", strlen ("UPGRADE")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_upgrade"));
	  else if (strncmp (temp, "ROB", strlen ("ROB")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_rob"));
	  else if (strncmp (temp, "SMUGGLE", strlen ("SMUGGLE")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_smuggle"));
	  else if (strncmp (temp, "ATTACK", strlen ("ATTACK")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_attack"));
	  else if (strncmp (temp, "QUIT", strlen ("QUIT")) == 0)
	    json_object_set_new (data_json, "pcommand",
				 json_string ("p_quit"));

	  json_object_set_new (data_json, "buffer", json_string (inbuffer));
	  json_object_set_new (data_json, "command", json_string ("ct_port"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "MYINFO", strlen ("MYINFO")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command", json_string ("ct_info"));
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "GAMEINFO", strlen ("GAMEINFO")) == 0
	       && loggedin)
	{
	  json_object_set_new (data_json, "command",
			       json_string ("ct_gameinfo"));
	  commandgood = 1;
	}
      else if ((sector = strtol (inbuffer, NULL, 10)) != 0 && loggedin)
	{
	  printf ("Thread %ld: Player moving to %d\n", senderid, sector);
	  json_object_set_new (data_json, "command", json_string ("ct_move"));
	  json_object_set_new (data_json, "to", json_integer (sector));
	  commandgood = 1;
	}

      if (commandgood)
	{
	  senddata_json (msgidin, data_json);
	  response_json = getdata_json (msgidout, senderid);
	  if (response_json)
	    {
	      const char *response_str =
		json_string_value (json_object_get
				   (response_json, "response"));
	      if (response_str)
		{
		  strcpy (outbuffer, response_str);
		}
	      else
		{
		  strcpy (outbuffer,
			  "BAD: Server returned an invalid response\n");
		}
	      json_decref (response_json);
	    }
	  else
	    {
	      strcpy (outbuffer, "BAD: Server response timed out\n");
	    }
	}
      else
	{
	  strcpy (outbuffer, "BAD\n");
	}

      json_decref (data_json);

      if (!loggedin && strncmp (outbuffer, "BAD\n", 3) != 0
	  && commandgood == 2)
	loggedin = 1;

      if (sendinfo (sockid, outbuffer) == -1)
	pthread_exit (NULL);

    }
  while (strcmp (inbuffer, "QUIT") != 0);

  //close our socket
  close (sockid);

  fprintf (stderr, "Thread %ld: Just closed the socket, exiting\n", senderid);

  return NULL;
}


json_t *list_hardware (json_t * json_data, struct player *curplayer)
{
  // Placeholder to satisfy the linker.
  return NULL;
}

json_t *buyhardware (json_t * json_data, struct player *curplayer)
{
  // Placeholder to satisfy the linker.
  return NULL;
}
