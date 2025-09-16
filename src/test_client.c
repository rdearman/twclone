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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include "common.h"

int
main (int argc, char *argv[])
{
  char buffer[BUFF_SIZE];
  int sockid, port = DEFAULT_PORT;

  switch (argc)
    {
    case 3:			//specified port and host
      //change the string to a long
      port = strtoul (argv[2], NULL, 10);
      if (port == 0)		//if it wasn't possible to change to an int
	{
	  //quit, and tell the user how to use us
	  printf ("usage:  %s server [port_num]\n", argv[0]);
	  exit (-1);
	}
      printf ("port %d specified\n", port);
    case 2:			//specified host only
      break;
    default:			//something else entirely
      printf ("usage: %s server [port_num]\n", argv[0]);
      exit (-1);
    };

  sockid = init_clientnetwork (argv[1], port);

  //tell the server hi
  buffer[0] = '\0';
  printf ("What do you want to tell the server: ");

  fgets (buffer, BUFF_SIZE, stdin);
  buffer[strcspn (buffer, "\n")] = '\0';

  if (sendinfo (sockid, buffer) == -1)
    exit (-1);

  fprintf (stderr, "I just told the server '%s'\n", buffer);

  do
    {
      if (recvinfo (sockid, buffer) == -1)
	exit (-1);

      fprintf (stderr, "The server said '%s', what do you want to say: ",
	       buffer);

      fgets (buffer, BUFF_SIZE, stdin);
      buffer[strcspn (buffer, "\n")] = '\0';

      if (sendinfo (sockid, buffer) == -1)
	exit (-1);

      fprintf (stderr, "I just told the server '%s'\n", buffer);

    }
  while (strcmp (buffer, "QUIT") != 0);

  //close the one and only socket that the client ever opened
  close (sockid);

  return 0;
}
