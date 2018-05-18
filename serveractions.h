#ifndef SERVER_ACTIONS_H
#define SERVER_ACTIONS_H

#include "msgqueue.h"

void processcommand (char *buffer, struct msgcommand *data);
void builddescription (int sector, char *buffer, int playernum);
void findautoroute (int from, int to, char *buffer);
void planetupgrade(char *buffer, struct planet *curplanet);
void planettake(char *buffer, struct player *curplayer);
void planetleave(char *buffer, struct player *curplayer);
void totalplanetinfo(int pnumb, char *buffer); 
void buildplayerinfo (int playernum, char *buffer);
void buildnewplanet (struct player *curplayer, char *buffer, int sector);
void buildshipinfo (int shipnum, char *buffer);
void buildtotalinfo (int pnumb, char *buffer, struct msgcommand *data);
void buildgameinfo(char *buffer);
void do_ship_upgrade(struct player *curplayer, char *buffer, struct ship *curship); 
void trading (struct player *curplayer, struct port *curport, char *buffer,
	      struct ship *curship);
void buildnewplayer (struct player *curplayer, char *shipname);
int move_player (struct player *p, struct msgcommand *data, char *buffer);
void buildportinfo (int portnumb, char *buffer);
int intransit (struct msgcommand *data);
void fedcommlink (int playernum, char *message);
void bank_deposit(char *buffer, struct player *curplayer);
void bank_balance(char *buffer, struct player *curplayer);
void bank_withdrawl(char *buffer, struct player *curplayer);
void sellship(char *buffer, struct player *curplayer);
void priceship(char *buffer, struct player *curplayer);
void listships(char *buffer);
void buyship(char *buffer, struct player *curplayer);
void listhardware(char *buffer, struct player *curplayer);
void buyhardware(char *buffer, struct player *curplayer);
void whosonline (char *buffer);
void sendtoallonline (char *message);
void addmessage (struct player *curplayer, char *message);
void sendtosector (int sector, int playernum, int direction, int planetnum);
void saveplayer (int pnumb, char *filename);
void saveship (int snumb, char *filename);
void saveallports (char *filename);
int innode(int sector);
void listnodes(char *buffer, struct port *curport);
void nodetravel(char *buffer, struct player *curplayer);

#endif
