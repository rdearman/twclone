#ifndef MSG_QUEUE_H
#define MSG_QUEUE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "common.h"



enum commandtype
{
  pl_display,
  pl_ownership,
  pl_destroy,
  pl_take,
  pl_leave,
  pl_citadel,
  pl_rest,
  pl_militarylvl,
  pl_qcannon,
  pl_evict,
  pl_swap,
  pl_upgrade,
  pl_cquit,
  pl_quit,
  ct_leave,
  ct_buy,
  ct_sell,
  ct_buy_hardware,
  ct_sellship,
  ct_buyship,
  ct_whosonline,
  ct_list_hardware,
  ct_quit,
  ct_describe,
  ct_move,
  ct_login,
  ct_newplayer,
  ct_logout,
  ct_playerinfo,
  ct_shipinfo,
  ct_port,
  ct_info,
  ct_portinfo,
  ct_gameinfo,
  ct_update,
  ct_fedcomm,
  ct_online,
  ct_stardock,
  ct_node,
  ct_planet,
  ct_onplanet,
  ct_land,
  ct_scan,
  ct_beacon,
  ct_tow,
  ct_listnav,
  ct_transporter,
  ct_genesis,
  ct_jettison,
  ct_interdict,
  ct_attack,
  ct_etherprobe,
  ct_fighters,
  ct_listfighters,
  ct_mines,
  ct_listmines,
  ct_portconstruction,
  ct_setnavs,
  ct_gamestatus,
  ct_hail,
  ct_subspace,
  ct_listwarps,
  ct_cim,
  ct_avoid,
  ct_listavoids,
  ct_selfdestruct,
  ct_listsettings,
  ct_updatesettings,
  ct_dailyannouncement,
  ct_firephoton,
  ct_readmail,
  ct_sendmail,
  ct_shiptime,
  ct_usedisruptor,
  ct_dailylog,
  ct_alienranks,
  ct_listplayers,
  ct_listplanets,
  ct_listships,
  ct_listcorps,
  ct_joincorp,
  ct_makecorp,
  ct_credittransfer,
  ct_fightertransfer,
  ct_minetransfer,
  ct_shieldtransfer,
  ct_quitcorp,
  ct_listcorpplanets,
  ct_showassets,
  ct_corpmemo,
  ct_dropmember,
  ct_corppassword,
};



struct msgcommand
{
  int command;
  char argument_string[256];	// Assuming max argument string size
};


// Define a message buffer struct for JSON strings
struct json_msgbuffer
{
  long mtype;
  char buffer[BUFF_SIZE];
};

// Function prototypes
int init_msgqueue ();
void clean_msgqueues (int msgidin, int msgidout, char *filename);
json_t *get_json_msg (int msgid, long *senderid);
void send_json_msg (int msgid, json_t * message, long mtype);


// Old API wrappers (now using JSON functions)
// int getmsg (int msgid, char *buffer, int *n);
//void sendmesg(int msgid, json_t* message, long mtype, long senderid);
long getdata (int msgid, struct msgcommand *data, long senderid);


char *getmsg (int msgid, long mtype, int *n);

#endif
