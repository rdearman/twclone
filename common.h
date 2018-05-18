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
// New Defines

#ifndef TURNS_PER_DAY
#define TURNS_PER_DAY 100
#endif

#ifndef MAX_WARPS
#define MAX_WARPS 6
#endif

#ifndef START_CREDITS
#define START_CREDITS 2000
#endif

#ifndef START_FIGHTERS
#define START_FIGHTERS 20
#endif

#ifndef START_HOLDS
#define START_HOLDS 20
#endif

#ifndef PROCESS_INTERVAL
#define PROCESS_INTERVAL 2
#endif

#ifndef AUTOSAVE
#define AUTOSAVE 10
#endif

#ifndef MAX_SHIP_NAME
#define MAX_SHIP_NAME 12
#endif

#ifndef SHIP_TYPES
#define SHIP_TYPES 10
#endif

#ifndef DEFAULT_NODES
#define DEFAULT_NODES 0
#endif


//  END New
#ifndef MAX_PLAYERS
#define MAX_PLAYERS 200
#endif

#ifndef MAX_SHIPS
#define MAX_SHIPS 1024
#endif

#ifndef MAX_PORTS
#define MAX_PORTS 500
#endif

#ifndef MAX_TOTAL_PLANETS
#define MAX_TOTAL_PLANETS 200
#endif

#ifndef MAX_WARPS_PER_SECTOR
#define MAX_WARPS_PER_SECTOR 6
#endif

#ifndef DEFAULT_PORT
#define DEFAULT_PORT 1234
#endif

#ifndef BUFF_SIZE
#define BUFF_SIZE 2000
#endif

#ifndef MAX_NAME_LENGTH
#define MAX_NAME_LENGTH 25
#endif

#ifndef COMMON_H
#define COMMON_H

#include <netinet/in.h>
#include <string.h>

int init_sockaddr (int, struct sockaddr_in *);
int init_clientnetwork (char *hostname, int port);
int sendinfo (int sockid, char *buffer);
int recvinfo (int sockid, char *buffer);
int acceptnewconnection (int sockid);
int randomnum(int min, int max);
int min(int a, int b);
int max(int a, int b);
enum porttype
{
  p_trade,
  p_land,
  p_upgrade,
  p_negotiate,
  p_rob,
  p_smuggle,
  p_attack,
  p_quit,
  p_buyship,
  p_sellship,
  p_priceship,
  p_listships,
  p_deposit,
  p_withdraw,
  p_balance,
  p_buyhardware,
  pn_listnodes,
  pn_travel
};

int *usedNames;

#endif
