#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ctype.h>
#include "common.h"


static inline uint64_t
monotonic_millis (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL;
}



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

int
randomnum (int min, int max)
{
  return (min +
	  ((int)
	   ((double) rand () / ((double) RAND_MAX + 1) * (1 + max - min))));

}

int
min (int a, int b)
{
  if (a > b)
    return (b);
  else if (a < b)
    return (a);
  else
    return (0);
}

int
max (int a, int b)
{
  if (a > b)
    return (a);
  else if (b > a)
    return (b);
  else
    return (0);
}



// Placeholder functions to satisfy the linker
void
getdata (int sockid, char *buffer, int maxlen)
{
  // This function is called but not implemented.
  // It's a placeholder to satisfy the linker.
  // In a real application, it would get data from a socket.
}

void
senddata (int sockid, char *buffer)
{
  // Placeholder. In a real application, this would send data over a socket.
}

void
doprocess ()
{
  // Placeholder for a function that handles processing.
}


#include <time.h>

void
now_iso8601 (char out[25])
{
  time_t t = time (NULL);
  struct tm tm;
  gmtime_r (&t, &tm);
  /* YYYY-MM-DDTHH:MM:SSZ -> 20 chars + NUL */
  strftime (out, 25, "%Y-%m-%dT%H:%M:%SZ", &tm);
}



void
strip_ansi (char *dst, const char *src, size_t cap)
{
  if (!dst || !src || cap == 0)
    return;
  size_t w = 0;
  for (size_t r = 0; src[r] != '\0' && w + 1 < cap;)
    {
      unsigned char c = (unsigned char) src[r];
      if (c == 0x1B)
	{			/* ESC */
	  /* Skip CSI: ESC '[' ... letter */
	  r++;
	  if (src[r] == '[')
	    {
	      r++;
	      while (src[r] && !(src[r] >= '@' && src[r] <= '~'))
		r++;		/* params */
	      if (src[r])
		r++;		/* consume final byte */
	      continue;
	    }
	  /* Skip OSC: ESC ']' ... BEL or ST (ESC '\') */
	  if (src[r] == ']')
	    {
	      r++;
	      while (src[r] && src[r] != 0x07)
		{
		  if (src[r] == 0x1B && src[r + 1] == '\\')
		    {
		      r += 2;
		      break;
		    }
		  r++;
		}
	      if (src[r] == 0x07)
		r++;		/* BEL */
	      continue;
	    }
	  /* Fallback: drop single ESC */
	  continue;
	}
      dst[w++] = src[r++];
    }
  dst[w] = '\0';
}
