#ifndef SERVER_COMMUNICATION_H
#define SERVER_COMMUNICATION_H

#include <jansson.h>
#include "common.h"		// client_ctx_t

#ifdef __cplusplus
extern "C"
{
#endif

/* ---- chat.* ---- */
  int cmd_chat_send (client_ctx_t * ctx, json_t * root);	// "chat.send"
  int cmd_chat_broadcast (client_ctx_t * ctx, json_t * root);	// "chat.broadcast"
  int cmd_chat_history (client_ctx_t * ctx, json_t * root);	// "chat.history"

/* ---- mail.* ---- */
  int cmd_mail_send (client_ctx_t * ctx, json_t * root);	// "mail.send"
  int cmd_mail_inbox (client_ctx_t * ctx, json_t * root);	// "mail.inbox"
  int cmd_mail_read (client_ctx_t * ctx, json_t * root);	// "mail.read"
  int cmd_mail_delete (client_ctx_t * ctx, json_t * root);	// "mail.delete"

/* ---- subscribe.* ---- */
  int cmd_subscribe_add (client_ctx_t * ctx, json_t * root);	// "subscribe.add"
  int cmd_subscribe_remove (client_ctx_t * ctx, json_t * root);	// "subscribe.remove"
  int cmd_subscribe_list (client_ctx_t * ctx, json_t * root);	// "subscribe.list"
  int cmd_subscribe_catalog (client_ctx_t * ctx, json_t * root);


  /*admin */
  int cmd_admin_notice (client_ctx_t * ctx, json_t * root);
  int cmd_admin_shutdown_warning (client_ctx_t * ctx, json_t * root);

  /* Optional: free subs for a disconnected client; call from your disconnect path */
  void comm_clear_subscriptions (client_ctx_t * ctx);

/* Publish an event to subscribers of sector.* and sector.{sector_id}.
   Ownership: this function steals a ref to 'data' (it will json_decref it). */
  void comm_publish_sector_event (int sector_id, const char *event_name,
				  json_t * data);


  /* ---- Ephemeral gameplay/admin broadcast ---- */
  typedef enum
  {
    COMM_SCOPE_GLOBAL = 0,	/* topic: broadcast.global */
    COMM_SCOPE_SECTOR,		/* topic: sector.{id}     */
    COMM_SCOPE_CORP,		/* topic: corp.{id}       */
    COMM_SCOPE_PLAYER		/* topic: player.{id}     */
  } comm_scope_t;

/* Sends a transient broadcast to subscribed clients only.
   - message: short, human-readable string (required)
   - extra: optional JSON payload to attach (steals a ref; pass NULL if none)
   Envelope: { status:"ok", type:"broadcast.message_v1", data:{...}, meta:{topic}}
*/
  void comm_broadcast_message (comm_scope_t scope, long long scope_id,
			       const char *message, json_t * extra);


#ifdef __cplusplus
}
#endif

#endif				/* SERVER_COMMUNICATION_H */
