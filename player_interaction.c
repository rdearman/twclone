/*
Copyright (C) 2000 Jason C. Garcowski(jcg5@po.cwru.edu), 
                   Ryan Glasnapp(rglasnap@nmt.edu)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "parse.h"
#include "msgqueue.h"
#include "common.h"
#include "player_interaction.h"



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

void *handle_player (void *threadinfo)
{
  int sector, sockid = ((struct connectinfo *) threadinfo)->sockid,
    msgidout = ((struct connectinfo *) threadinfo)->msgidout,
    msgidin = ((struct connectinfo *) threadinfo)->msgidin,
    commandgood, loggedin;
  char inbuffer[BUFF_SIZE], outbuffer[BUFF_SIZE],
    name[MAX_NAME_LENGTH + 1], passwd[MAX_NAME_LENGTH + 1], temp[BUFF_SIZE];

  struct msgcommand data;

  free (threadinfo);

  data.sockid = sockid;
  data.threadid = pthread_self();
  printf ("Thread %d: Created\n", (int) pthread_self ());
  loggedin = 0;

  do
    {
      commandgood = 0;

      outbuffer[0] = '\0';

      if (recvinfo (sockid, inbuffer) == -1)
		{
			fprintf(stderr, "Thread %d: Exiting!\n", (int)pthread_self());
			fflush(stderr);
			pthread_exit (NULL);
		}

      //fprintf(stderr, "handle_player: I got '%s' as the messagem and loggedin = %d\n", 
      //inbuffer, loggedin);

      //parse stuff from client, should be expanded, modularized
      if (strncmp (inbuffer, "DESCRIPTION", strlen ("DESCRIPTION")) == 0
	  && loggedin)
	{
	  printf ("Thread %d: Player Querried\n", (int) pthread_self ());
	  strcpy (data.name, name);
	  data.command = ct_describe;
	  commandgood = 1;
	}
      else if (((strncmp (inbuffer, "USER", strlen ("USER")) == 0) ||
		(strncmp (inbuffer, "NEW", strlen ("NEW")) == 0))
	       && !loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);

	  data.command = (strlen (temp) == 4) ? ct_login : ct_newplayer;

	  popstring (inbuffer, name, ":", BUFF_SIZE);
	  popstring (inbuffer, passwd, ":", BUFF_SIZE);
	  popstring (inbuffer, data.buffer, ":", BUFF_SIZE);

	  fprintf (stderr,
		   "Thread %d: Player '%s' trying to login with passwd '%s'\n",
		   (int) pthread_self (), name, passwd);
	  strcpy (data.name, name);
	  strcpy (data.passwd, passwd);

	  commandgood = 2;
	}
      else if (strncmp (inbuffer, "PLAYERINFO", strlen ("PLAYERINFO")) == 0
	       && loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  data.to = popint (inbuffer, ":");
	  data.command = ct_playerinfo;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "UPDATE", strlen ("UPDATE")) == 0
	       && loggedin)
	{
	  strcpy (data.name, name);
	  data.command = ct_update;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "ONLINE", strlen ("ONLINE")) == 0)
	{
	  data.command = ct_online;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "FEDCOMM", strlen ("FEDCOMM")) == 0
	       && loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  popstring (inbuffer, temp, ":", BUFF_SIZE);
	  strcpy (data.buffer, temp);
	  data.command = ct_fedcomm;
	  commandgood = 1;
	}
		else if (strncmp(inbuffer, "STARDOCK", strlen("STARDOCK")) == 0 && loggedin)
		{
			strcpy(data.name, name);
			popstring(inbuffer, temp, " ", BUFF_SIZE);
			popstring(inbuffer, temp, ":", BUFF_SIZE);
			if (strncmp(temp, "BUYSHIP", strlen("BUYSHIP")) == 0)
				data.pcommand = p_buyship;
			else if (strncmp(temp, "SELLSHIP", strlen("SELLSHIP")) == 0)
				data.pcommand = p_sellship;
			else if (strncmp(temp, "PRICESHIP", strlen("PRICESHIP")) == 0)
				data.pcommand = p_priceship;
			else if (strncmp(temp, "LISTSHIPS", strlen("PRICESHIP")) == 0)
				data.pcommand = p_listships;
			else if (strncmp(temp, "DEPOSIT", strlen("DEPOSIT")) == 0)
				data.pcommand = p_deposit;
			else if (strncmp(temp, "WITHDRAW", strlen("WITHDRAW")) == 0)
				data.pcommand = p_withdraw;
			else if (strncmp(temp, "BALANCE", strlen("BALANCE")) == 0)
				data.pcommand = p_balance;
			else if (strncmp(temp, "BUYHARDWARE", strlen("BUYHARDWARE")) == 0)
				data.pcommand = p_buyhardware;
			strcpy(data.buffer, inbuffer);
			data.command = ct_stardock;
			commandgood = 1;
		}
		else if (strncmp(inbuffer, "NODE", strlen("NODE")) == 0 && loggedin)
		{
			strcpy(data.name, name);
			popstring(inbuffer, temp, " ", BUFF_SIZE);
			popstring(inbuffer, temp, ":", BUFF_SIZE);
			if (strncmp(temp, "BUYSHIP", strlen("BUYSHIP")) == 0)
				data.pcommand = p_buyship;
			else if (strncmp(temp, "SELLSHIP", strlen("SELLSHIP")) == 0)
				data.pcommand = p_sellship;
			else if (strncmp(temp, "PRICESHIP", strlen("PRICESHIP")) == 0)
				data.pcommand = p_priceship;
			else if (strncmp(temp, "LISTSHIPS", strlen("PRICESHIP")) == 0)
				data.pcommand = p_listships;
			else if (strncmp(temp, "DEPOSIT", strlen("DEPOSIT")) == 0)
				data.pcommand = p_deposit;
			else if (strncmp(temp, "WITHDRAW", strlen("WITHDRAW")) == 0)
				data.pcommand = p_withdraw;
			else if (strncmp(temp, "BALANCE", strlen("BALANCE")) == 0)
				data.pcommand = p_balance;
			else if (strncmp(temp, "BUYHARDWARE", strlen("BUYHARDWARE")) == 0)
				data.pcommand = p_buyhardware;
			else if (strncmp(temp, "LISTNODES", strlen("LISTNODES")) == 0)
				data.pcommand = pn_listnodes;
			else if (strncmp(temp, "TRAVEL", strlen("TRAVEL")) == 0)
				data.pcommand = pn_travel;
			strcpy(data.buffer, inbuffer);
			data.command = ct_node;
			commandgood = 1;
		}
      else if (strncmp (inbuffer, "LAND", strlen ("LAND")) == 0 && loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  data.to = popint (inbuffer, ":");
	  data.command = ct_land;
	  commandgood = 1;
	}
		else if (strncmp(inbuffer, "ONPLANET", strlen("PLANET")) == 0 && loggedin)
		{
			strcpy(data.name, name);
			data.command = ct_onplanet;
			commandgood = 1;
		}
		else if (strncmp(inbuffer, "PLANET", strlen("PLANET")) == 0 && loggedin)
		{
			strcpy(data.name, name);
			popstring(inbuffer, temp, " ", BUFF_SIZE);
			popstring(inbuffer, temp, ":", BUFF_SIZE);
			if (strncmp(temp, "DISPLAY", strlen("DISPLAY")) == 0)
				data.plcommand = pl_display;
			else if (strncmp(temp, "OWNERSHIP", strlen("OWNERSHIP")) == 0)
				data.plcommand = pl_ownership;
			else if (strncmp(temp, "DESTROY", strlen("DESTROY")) == 0)
					  data.plcommand = pl_destroy;
			else if (strncmp(temp, "TAKE", strlen("TAKE")) == 0)
					  data.plcommand = pl_take;
			else if (strncmp(temp, "LEAVE", strlen("LEAVE")) == 0)
					  data.plcommand = pl_leave;
			else if (strncmp(temp, "CITADEL", strlen("CITADEL")) == 0)
					  data.plcommand = pl_citadel;
			else if (strncmp(temp, "REST", strlen("REST")) == 0)
					  data.plcommand = pl_rest;
			else if (strncmp(temp, "MRL", strlen("MRL")) == 0)
					  data.plcommand = pl_militarylvl;
			else if (strncmp(temp, "QCANNON", strlen("QCANNON")) == 0)
					  data.plcommand = pl_qcannon;
			else if (strncmp(temp, "EVICT", strlen("EVICT")) == 0)
					  data.plcommand = pl_evict;
			else if (strncmp(temp, "SWAP", strlen("SWAP")) == 0)
					  data.plcommand = pl_swap;
			else if (strncmp(temp, "UPGRADE", strlen("UPGRADE")) == 0)
					  data.plcommand = pl_upgrade;
			else if (strncmp(temp, "CQUIT", strlen("CQUIT")) == 0)
					  data.plcommand = pl_cquit;
			else if (strncmp(temp, "QUIT", strlen("QUIT")) == 0)
					  data.plcommand = pl_quit;
			strcpy(data.buffer, inbuffer);
			data.command = ct_planet;
			commandgood = 1;
		}
      else if (strncmp (inbuffer, "SCAN", strlen ("SCAN")) == 0 && loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  data.command = ct_scan;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "BEACON", strlen ("BEACON")) == 0
	       && loggedin)
	{
	  data.command = ct_beacon;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "TOW", strlen ("TOW")) == 0 && loggedin)
	{
	  data.command = ct_tow;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTNAV", strlen ("LISTNAV")) == 0
	       && loggedin)
	{
	  data.command = ct_listnav;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "TRANSPORTER", strlen ("TRANSPORTER")) == 0
	       && loggedin)
	{
	  data.command = ct_transporter;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "GENESIS", strlen ("GENESIS")) == 0
	       && loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  strcpy(data.buffer, inbuffer);
	  data.command = ct_genesis;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "JETTISON", strlen ("JETTISON")) == 0
	       && loggedin)
	{
	  data.command = ct_jettison;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "INTERDICT", strlen ("INTERDICT")) == 0
	       && loggedin)
	{
	  data.command = ct_interdict;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "ATTACK", strlen ("ATTACK")) == 0
	       && loggedin)
	{
	  data.command = ct_attack;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "ETHERPROBE", strlen ("ETHERPROBE")) == 0
	       && loggedin)
	{
	  data.command = ct_etherprobe;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "FIGHTERS", strlen ("FIGHTERS")) == 0
	       && loggedin)
	{
	  data.command = ct_fighters;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTFIGHTERS", strlen ("LISTFIGHTERS")) ==
	       0 && loggedin)
	{
	  data.command = ct_listfighters;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "MINES", strlen ("MINES")) == 0 && loggedin)
	{
	  data.command = ct_mines;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTMINES", strlen ("LISTMINES")) == 0
	       && loggedin)
	{
	  data.command = ct_listmines;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "PORTUPGRADE", strlen ("PORTUPGRADE")) == 0
	       && loggedin)
	{
	  data.command = ct_portconstruction;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "SETNAVS", strlen ("SETNAVS")) == 0
	       && loggedin)
	{
	  data.command = ct_setnavs;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "STATUS", strlen ("STATUS")) == 0
	       && loggedin)
	{
	  data.command = ct_gamestatus;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "HAIL", strlen ("HAIL")) == 0 && loggedin)
	{
	  data.command = ct_hail;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "SUBSPACE", strlen ("SUBSPACE")) == 0
	       && loggedin)
	{
	  data.command = ct_subspace;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTWARPS", strlen ("LISTWARPS")) == 0
	       && loggedin)
	{
	  data.command = ct_listwarps;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "CIM", strlen ("CIM")) == 0 && loggedin)
	{
	  data.command = ct_cim;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "AVOID", strlen ("AVOID")) == 0 && loggedin)
	{
	  data.command = ct_avoid;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTAVOIDS", strlen ("LISTAVOIDS")) == 0
	       && loggedin)
	{
	  data.command = ct_listavoids;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "SELFDESTRUCT", strlen ("SELFDESTRUCT")) ==
	       0 && loggedin)
	{
	  data.command = ct_selfdestruct;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTSETTINGS", strlen ("LISTSETTINGS")) ==
	       0 && loggedin)
	{
	  data.command = ct_listsettings;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "SETTINGS", strlen ("SETTINGS")) == 0
	       && loggedin)
	{
	  data.command = ct_updatesettings;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "ANNOUNCE", strlen ("ANNOUNCE")) == 0
	       && loggedin)
	{
	  data.command = ct_dailyannouncement;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "PHOTON", strlen ("PHOTON")) == 0
	       && loggedin)
	{
	  data.command = ct_firephoton;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "READMAIL", strlen ("READMAIL")) == 0
	       && loggedin)
	{
	  data.command = ct_readmail;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "SENDMAIL", strlen ("SENDMAIL")) == 0
	       && loggedin)
	{
	  data.command = ct_sendmail;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "TIME", strlen ("TIME")) == 0 && loggedin)
	{
	  data.command = ct_shiptime;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "DISRUPTOR", strlen ("DISRUPTOR")) == 0
	       && loggedin)
	{
	  data.command = ct_usedisruptor;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "DAILYLOG", strlen ("DAILYLOG")) == 0
	       && loggedin)
	{
	  data.command = ct_dailylog;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "ALIENRANKS", strlen ("ALIENRANKS")) == 0
	       && loggedin)
	{
	  data.command = ct_alienranks;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTPLAYERS", strlen ("LISTPLAYERS")) == 0
	       && loggedin)
	{
	  data.command = ct_listplayers;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTPLANETS", strlen ("LISTPLANETS")) == 0
	       && loggedin)
	{
	  data.command = ct_listplanets;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTSHIPS", strlen ("LISTSHIPS")) == 0
	       && loggedin)
	{
	  data.command = ct_listships;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "LISTCORPS", strlen ("LISTCORPS")) == 0
	       && loggedin)
	{
	  data.command = ct_listcorps;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "JOINCORP", strlen ("JOINCORP")) == 0
	       && loggedin)
	{
	  data.command = ct_joincorp;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "MAKECORP", strlen ("MAKECORP")) == 0
	       && loggedin)
	{
	  data.command = ct_makecorp;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "CREDTRANS", strlen ("CREDTRANS")) == 0
	       && loggedin)
	{
	  data.command = ct_credittransfer;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "FIGTRANS", strlen ("FIGTRANS")) == 0
	       && loggedin)
	{
	  data.command = ct_fightertransfer;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "MINETRANS", strlen ("MINETRANS")) == 0
	       && loggedin)
	{
	  data.command = ct_minetransfer;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "SHIELDTRANS", strlen ("SHIELDTRANS")) == 0
	       && loggedin)
	{
	  data.command = ct_shieldtransfer;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "CORPQUIT", strlen ("CORPQUIT")) == 0
	       && loggedin)
	{
	  data.command = ct_quitcorp;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "CORPPLANETS", strlen ("CORPPLANETS")) == 0
	       && loggedin)
	{
	  data.command = ct_listcorpplanets;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "ASSETS", strlen ("ASSETS")) == 0
	       && loggedin)
	{
	  data.command = ct_showassets;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "CORPMEMO", strlen ("CORPMEMO")) == 0
	       && loggedin)
	{
	  data.command = ct_corpmemo;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "DROPMEMBER", strlen ("DROPMEMBER")) == 0
	       && loggedin)
	{
	  data.command = ct_dropmember;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "CORPPASS", strlen ("CORPPASS")) == 0
	       && loggedin)
	{
	  data.command = ct_corppassword;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "SHIPINFO", strlen ("SHIPINFO")) == 0
	       && loggedin)
	{
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  data.to = popint (inbuffer, ":");
	  data.command = ct_shipinfo;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "QUIT", strlen ("QUIT")) == 0 && loggedin)
	{
	  strcpy (data.name, name);
	  data.command = ct_logout;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "PORTINFO", strlen ("PORTINFO")) == 0
	       && loggedin)
	{			//don't move this below port.. otherwise
	  strcpy (data.name, name);	//badstuff will happen
	  data.command = ct_portinfo;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "PORT", strlen ("PORT")) == 0 && loggedin)
	{
	  printf ("Thread %d: Player attempting to port\n",
		  (int) pthread_self ());
	  strcpy (data.name, name);
	  popstring (inbuffer, temp, " ", BUFF_SIZE);
	  popstring (inbuffer, temp, ":", BUFF_SIZE);
	  if (strncmp (temp, "TRADE", strlen ("TRADE")) == 0)
	    data.pcommand = p_trade;
	  else if (strncmp (temp, "LAND", strlen ("LAND")) == 0)
	    data.pcommand = p_land;
	  else if (strncmp (temp, "NEGOTIATE", strlen ("NEGOTIATE")) == 0)
	    data.pcommand = p_trade;
	  else if (strncmp (temp, "UPGRADE", strlen ("UPGRADE")) == 0)
	    data.pcommand = p_upgrade;
	  else if (strncmp (temp, "ROB", strlen ("ROB")) == 0)
	    data.pcommand = p_rob;
	  else if (strncmp (temp, "SMUGGLE", strlen ("SMUGGLE")) == 0)
	    data.pcommand = p_smuggle;
	  else if (strncmp (temp, "ATTACK", strlen ("ATTACK")) == 0)
	    data.pcommand = p_attack;
	  else if (strncmp (temp, "QUIT", strlen ("QUIT")) == 0)
	    data.pcommand = p_quit;

	  strncpy (data.buffer, inbuffer, 30);
	  data.command = ct_port;
	  commandgood = 1;
	}
      else if (strncmp (inbuffer, "MYINFO", strlen ("MYINFO")) == 0
	       && loggedin)
	{
	  strcpy (data.name, name);
	  data.command = ct_info;
	  commandgood = 1;
	}
		else if (strncmp(inbuffer, "GAMEINFO", strlen("GAMEINFO")) == 0
				&& loggedin)
		{
			strcpy(data.name, name);
			data.command = ct_gameinfo;
			commandgood = 1;
		}
      else if ((sector = strtol (inbuffer, NULL, 10)) != 0 && loggedin)
	{
	  printf ("Thread %d: Player moving to %d\n", (int) pthread_self (),
		  sector);
	  strcpy (data.name, name);
	  data.command = ct_move;
	  data.to = sector;
	  commandgood = 1;
	}

      if (commandgood)
	{
	  senddata (msgidin, &data, pthread_self ());
	  getmsg (msgidout, outbuffer, pthread_self ());
	}
      else
	strcpy (outbuffer, "BAD\n");

      if (!loggedin && strncmp (outbuffer, "BAD\n", 3) != 0
	  && commandgood == 2)
	loggedin = 1;

      if (sendinfo (sockid, outbuffer) == -1)
	pthread_exit (NULL);

    }
  while (strcmp (inbuffer, "QUIT") != 0);

  //close our socket
  close (sockid);

  fprintf (stderr, "Thread %d: Just closed the socket, exiting\n",
	   (int) pthread_self ());

  return NULL;
}
