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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "msgqueue.h"
#include "common.h"
#include "player_interaction.h"
#include "sysop_interaction.h"
extern int WARP_WAIT;

void *getsysopcommands (void *threadinfo)
{
  int msgidin = ((struct connectinfo *) threadinfo)->msgidin;
  struct msgcommand data;
  char buffer[BUFF_SIZE];

  free (threadinfo);

  while (1)
    {
      if (WARP_WAIT)
	{
	  printf ("[+]> ");
	}
      else
	{
	  printf ("[-]> ");
	}
      fgets (buffer, BUFF_SIZE, stdin);
      buffer[strcspn (buffer, "\n")] = '\0';
      if (strcmp (buffer, "QUIT") == 0)
	{
	  data.command = ct_quit;
	  senddata (msgidin, &data, pthread_self ());
	  break;
	}
      if (strcmp(buffer, "WARP_WAIT") == 0)
	{
	  WARP_WAIT = 1;
	  printf ("WARP WAIT ON\n");
	}
      if (strcmp(buffer, "NOWARP_WAIT") == 0)
	{
	  WARP_WAIT = 0;
	  printf ("WARP WAIT OFF\n");
	}
      if (strcmp (buffer, "?") == 0)
	{
	  printf ("COMMANDS: QUIT | WARP_WAIT | NOWARP_WAIT | ? \n");
	}
    }
  return NULL;
}
