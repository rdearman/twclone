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
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "common.h"
#include "parse.h"

/*
  init_sockaddr

  behavior: accepts a port, and a pointer to a sockaddr_in structure,
  returns a sockid corresponding to a bound socket that was made in the
  function.
*/
int
init_sockaddr (int port, struct sockaddr_in *sock)
{
  int sockid;

  sock->sin_family = AF_INET;
  sock->sin_port = htons (port);
  sock->sin_addr.s_addr = htonl (INADDR_ANY);

  if ((sockid = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      perror ("init_sockaddr: socket");
      exit (-1);
    }

  if (bind (sockid, (struct sockaddr *) sock, sizeof (*sock)) == -1)
    {
      perror ("init_sockaddr: bind");
      close (sockid);
      exit (-1);
    }

  if (listen (sockid, 7) == -1)
    {
      perror ("init_sockaddr: listen");
      close (sockid);
      exit (-1);
    }

  return sockid;
}

int
init_clientnetwork (char *hostname, int port)
{
  struct hostent *host;
  struct sockaddr_in serv_sockaddr;
  int sockid;

  if ((host = gethostbyname (hostname)) == (struct hostent *) NULL)
    {
      perror ("WRITER: gethostbyname");
      exit (-1);
    }

  //setting up the sockaddt pointing to the server
  serv_sockaddr.sin_family = AF_INET;
  memcpy (&serv_sockaddr.sin_addr, host->h_addr, host->h_length);
  serv_sockaddr.sin_port = htons (port);

  if ((sockid = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      perror ("socket: ");
      exit (-1);
    }

  if (connect
      (sockid, (struct sockaddr *) &serv_sockaddr,
       sizeof (serv_sockaddr)) == -1)
    {
      perror ("connect: ");
      exit (-1);
    }

  return sockid;

}

int
sendinfo (int sockid, char *buffer)
{
  if (send (sockid, buffer, strlen (buffer), 0) == -1)
    {
      perror ("sendto: ");
      close (sockid);
      return -1;
    }
  return 0;
}


int
recvinfo (int sockid, char *buffer)
{
  int len;
  char tempbuffer[BUFF_SIZE];

  if ((len = recv (sockid, tempbuffer, BUFF_SIZE, 0)) == -1)
    {
      perror ("recvfrom: ");
      close (sockid);
      return -1;
    }
  tempbuffer[len] = '\0';
  strncpy (buffer, tempbuffer, BUFF_SIZE);
  return 0;
}

int
acceptnewconnection (int sockid)
{
  int sockaid, clnt_length;
  struct sockaddr_in *clnt_sockaddr = NULL;

  clnt_length = sizeof (*clnt_sockaddr);

  if ((sockaid = accept (sockid, (struct sockaddr *) clnt_sockaddr,
			 &clnt_length)) == -1)
    {
      perror ("accept: ");
      exit (-1);
    }

  return sockaid;
}

int randomnum(int min, int max)
{
	  return (min +
	  ((int)
	   ((double) rand () / ((double) RAND_MAX + 1) * (1 + max - min))));

}

int min(int a, int b)
{
	if (a>b)
		return(b);
	else if (a<b)
		return(a);
	else
		return(0);
}

int max(int a, int b)
{
	if (a>b)
		return(a);
	else if (b>a)
		return(b);
	else
		return(0);
}
