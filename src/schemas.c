#include <string.h>
#include <jansson.h>
#include "schemas.h"
#include <stdlib.h>
#include <string.h>

/*
 * =============================================================================
 * --- Forward Declarations ---
 *
 * This fixes the compilation error where schema_get() was called
 * before its static functions were defined.
 * =============================================================================
 */

static json_t *schema_envelope (void);

/* --- Auth --- */
static json_t *schema_auth_login (void);
static json_t *schema_auth_register (void);
static json_t *schema_auth_logout (void);
static json_t *schema_auth_refresh (void);
static json_t *schema_auth_mfa_totp_verify (void);

/* --- System --- */
static json_t *schema_system_capabilities (void);
static json_t *schema_system_describe_schema (void);
static json_t *schema_system_hello (void);
static json_t *schema_system_disconnect (void);

/* --- Session --- */
static json_t *schema_session_ping (void);
static json_t *schema_session_hello (void);
static json_t *schema_session_disconnect (void);

/* --- Ship --- */
static json_t *schema_ship_inspect (void);
static json_t *schema_ship_rename (void);
static json_t *schema_ship_reregister (void);
static json_t *schema_ship_claim (void);
static json_t *schema_ship_status (void);
static json_t *schema_ship_info (void);
static json_t *schema_ship_transfer_cargo (void);
static json_t *schema_ship_jettison (void);
static json_t *schema_ship_upgrade (void);
static json_t *schema_ship_repair (void);
static json_t *schema_ship_self_destruct (void);

/* --- Port --- */
static json_t *schema_port_info (void);
static json_t *schema_port_status (void);
static json_t *schema_port_describe (void);

/* --- Trade --- */
static json_t *schema_trade_port_info (void);
static json_t *schema_trade_buy (void);
static json_t *schema_trade_sell (void);
static json_t *schema_trade_quote (void);
static json_t *schema_trade_jettison (void);
static json_t *schema_trade_offer (void);
static json_t *schema_trade_accept (void);
static json_t *schema_trade_cancel (void);
static json_t *schema_trade_history (void);

/* --- Move --- */
static json_t *schema_move_describe_sector (void);
static json_t *schema_move_scan (void);
static json_t *schema_move_warp (void);
static json_t *schema_move_pathfind (void);
static json_t *schema_move_autopilot_start (void);
static json_t *schema_move_autopilot_stop (void);
static json_t *schema_move_autopilot_status (void);

/* --- Sector --- */
static json_t *schema_sector_info (void);
static json_t *schema_sector_search (void);
static json_t *schema_sector_set_beacon (void);
static json_t *schema_sector_scan_density (void);
static json_t *schema_sector_scan (void);

/* --- Planet --- */
static json_t *schema_planet_genesis (void);
static json_t *schema_planet_info (void);
static json_t *schema_planet_rename (void);
static json_t *schema_planet_land (void);
static json_t *schema_planet_launch (void);
static json_t *schema_planet_transfer_ownership (void);
static json_t *schema_planet_harvest (void);
static json_t *schema_planet_deposit (void);
static json_t *schema_planet_withdraw (void);

/* --- Citadel --- */
static json_t *schema_citadel_build (void);
static json_t *schema_citadel_upgrade (void);

/* --- Combat --- */
static json_t *schema_combat_attack (void);
static json_t *schema_combat_deploy_fighters (void);
static json_t *schema_combat_lay_mines (void);
static json_t *schema_combat_sweep_mines (void);
static json_t *schema_combat_status (void);

/* --- Deploy --- */
static json_t *schema_deploy_fighters_list (void);
static json_t *schema_deploy_mines_list (void);

/* --- Chat --- */
static json_t *schema_chat_send (void);
static json_t *schema_chat_broadcast (void);
static json_t *schema_chat_history (void);

/* --- Mail --- */
static json_t *schema_mail_send (void);
static json_t *schema_mail_inbox (void);
static json_t *schema_mail_read (void);
static json_t *schema_mail_delete (void);

/* --- Notice --- */
static json_t *schema_sys_notice_create (void);
static json_t *schema_notice_list (void);
static json_t *schema_notice_ack (void);

/* --- News --- */
static json_t *schema_news_read (void);

/* --- Subscribe --- */
static json_t *schema_subscribe_add (void);
static json_t *schema_subscribe_remove (void);
static json_t *schema_subscribe_list (void);
static json_t *schema_subscribe_catalog (void);

/* --- Bulk --- */
static json_t *schema_bulk_execute (void);

/*
 * =============================================================================
 * --- Helper Functions ---
 * =============================================================================
 */

static char *
why_dup (const char *m)
{
  size_t n = strlen (m) + 1;
  char *p = (char *) malloc (n);
  if (p)
    memcpy (p, m, n);
  return p;
}

/**
 * @brief Placeholder for a real JSON Schema validation function.
 *
 * You will need to replace this with a real validation library
 * (e.g., libjsonschema) to perform actual schema validation.
 */
static int
my_json_schema_validate_placeholder (json_t *schema, json_t *payload, char **why)
{
  /* TODO: Implement this using a real validator library. */
  (void)schema;  /* Suppress unused parameter warning */
  (void)payload; /* Suppress unused parameter warning */

  /* For now, we just pretend it's always valid */
  if (why)
    *why = NULL;
  return 0;
}

/*
 * =============================================================================
 * --- PUBLIC API: C2S Schema Registry ---
 * =============================================================================
 */

json_t *
schema_get (const char *key)
{
  if (!key)
    return NULL;

  /* Core */
  if (strcmp (key, "envelope") == 0)
    return schema_envelope ();

  /* Auth */
  else if (strcmp (key, "auth.login") == 0)
    return schema_auth_login ();
  else if (strcmp (key, "auth.register") == 0)
    return schema_auth_register ();
  else if (strcmp (key, "auth.logout") == 0)
    return schema_auth_logout ();
  else if (strcmp (key, "auth.refresh") == 0)
    return schema_auth_refresh ();
  else if (strcmp (key, "auth.mfa.totp.verify") == 0)
    return schema_auth_mfa_totp_verify ();

  /* System */
  else if (strcmp (key, "system.capabilities") == 0)
    return schema_system_capabilities ();
  else if (strcmp (key, "system.describe_schema") == 0)
    return schema_system_describe_schema ();
  else if (strcmp (key, "system.hello") == 0)
    return schema_system_hello ();
  else if (strcmp (key, "system.disconnect") == 0)
    return schema_system_disconnect ();

  /* Session */
  else if (strcmp (key, "session.ping") == 0)
    return schema_session_ping ();
  else if (strcmp (key, "session.hello") == 0)
    return schema_session_hello ();
  else if (strcmp (key, "session.disconnect") == 0)
    return schema_session_disconnect ();

  /* Ship */
  else if (strcmp (key, "ship.inspect") == 0)
    return schema_ship_inspect ();
  else if (strcmp (key, "ship.rename") == 0)
    return schema_ship_rename ();
  else if (strcmp (key, "ship.reregister") == 0)
    return schema_ship_reregister ();
  else if (strcmp (key, "ship.claim") == 0)
    return schema_ship_claim ();
  else if (strcmp (key, "ship.status") == 0)
    return schema_ship_status ();
  else if (strcmp (key, "ship.info") == 0)
    return schema_ship_info ();
  else if (strcmp (key, "ship.transfer_cargo") == 0)
    return schema_ship_transfer_cargo ();
  else if (strcmp (key, "ship.jettison") == 0)
    return schema_ship_jettison ();
  else if (strcmp (key, "ship.upgrade") == 0)
    return schema_ship_upgrade ();
  else if (strcmp (key, "ship.repair") == 0)
    return schema_ship_repair ();
  else if (strcmp (key, "ship.self_destruct") == 0)
    return schema_ship_self_destruct ();

  /* Port */
  else if (strcmp (key, "port.info") == 0)
    return schema_port_info ();
  else if (strcmp (key, "port.status") == 0)
    return schema_port_status ();
  else if (strcmp (key, "port.describe") == 0)
    return schema_port_describe ();

  /* Trade */
  else if (strcmp (key, "trade.port_info") == 0)
    return schema_trade_port_info ();
  else if (strcmp (key, "trade.buy") == 0)
    return schema_trade_buy ();
  else if (strcmp (key, "trade.sell") == 0)
    return schema_trade_sell ();
  else if (strcmp (key, "trade.quote") == 0)
    return schema_trade_quote ();
  else if (strcmp (key, "trade.jettison") == 0)
    return schema_trade_jettison ();
  else if (strcmp (key, "trade.offer") == 0)
    return schema_trade_offer ();
  else if (strcmp (key, "trade.accept") == 0)
    return schema_trade_accept ();
  else if (strcmp (key, "trade.cancel") == 0)
    return schema_trade_cancel ();
  else if (strcmp (key, "trade.history") == 0)
    return schema_trade_history ();

  /* Move */
  else if (strcmp (key, "move.describe_sector") == 0)
    return schema_move_describe_sector ();
  else if (strcmp (key, "move.scan") == 0)
    return schema_move_scan ();
  else if (strcmp (key, "move.warp") == 0)
    return schema_move_warp ();
  else if (strcmp (key, "move.pathfind") == 0)
    return schema_move_pathfind ();
  else if (strcmp (key, "move.autopilot.start") == 0)
    return schema_move_autopilot_start ();
  else if (strcmp (key, "move.autopilot.stop") == 0)
    return schema_move_autopilot_stop ();
  else if (strcmp (key, "move.autopilot.status") == 0)
    return schema_move_autopilot_status ();

  /* Sector */
  else if (strcmp (key, "sector.info") == 0)
    return schema_sector_info ();
  else if (strcmp (key, "sector.search") == 0)
    return schema_sector_search ();
  else if (strcmp (key, "sector.set_beacon") == 0)
    return schema_sector_set_beacon ();
  else if (strcmp (key, "sector.scan.density") == 0)
    return schema_sector_scan_density ();
  else if (strcmp (key, "sector.scan") == 0)
    return schema_sector_scan ();

  /* Planet */
  else if (strcmp (key, "planet.genesis") == 0)
    return schema_planet_genesis ();
  else if (strcmp (key, "planet.info") == 0)
    return schema_planet_info ();
  else if (strcmp (key, "planet.rename") == 0)
    return schema_planet_rename ();
  else if (strcmp (key, "planet.land") == 0)
    return schema_planet_land ();
  else if (strcmp (key, "planet.launch") == 0)
    return schema_planet_launch ();
  else if (strcmp (key, "planet.transfer_ownership") == 0)
    return schema_planet_transfer_ownership ();
  else if (strcmp (key, "planet.harvest") == 0)
    return schema_planet_harvest ();
  else if (strcmp (key, "planet.deposit") == 0)
    return schema_planet_deposit ();
  else if (strcmp (key, "planet.withdraw") == 0)
    return schema_planet_withdraw ();

  /* Citadel */
  else if (strcmp (key, "citadel.build") == 0)
    return schema_citadel_build ();
  else if (strcmp (key, "citadel.upgrade") == 0)
    return schema_citadel_upgrade ();

  /* Combat */
  else if (strcmp (key, "combat.attack") == 0)
    return schema_combat_attack ();
  else if (strcmp (key, "combat.deploy_fighters") == 0)
    return schema_combat_deploy_fighters ();
  else if (strcmp (key, "combat.lay_mines") == 0)
    return schema_combat_lay_mines ();
  else if (strcmp (key, "combat.sweep_mines") == 0)
    return schema_combat_sweep_mines ();
  else if (strcmp (key, "combat.status") == 0)
    return schema_combat_status ();

  /* Deploy */
  else if (strcmp (key, "deploy.fighters.list") == 0)
    return schema_deploy_fighters_list ();
  else if (strcmp (key, "deploy.mines.list") == 0)
    return schema_deploy_mines_list ();

  /* Chat */
  else if (strcmp (key, "chat.send") == 0)
    return schema_chat_send ();
  else if (strcmp (key, "chat.broadcast") == 0)
    return schema_chat_broadcast ();
  else if (strcmp (key, "chat.history") == 0)
    return schema_chat_history ();

  /* Mail */
  else if (strcmp (key, "mail.send") == 0)
    return schema_mail_send ();
  else if (strcmp (key, "mail.inbox") == 0)
    return schema_mail_inbox ();
  else if (strcmp (key, "mail.read") == 0)
    return schema_mail_read ();
  else if (strcmp (key, "mail.delete") == 0)
    return schema_mail_delete ();

  /* Notice */
  else if (strcmp (key, "sys.notice.create") == 0)
    return schema_sys_notice_create ();
  else if (strcmp (key, "notice.list") == 0)
    return schema_notice_list ();
  else if (strcmp (key, "notice.ack") == 0)
    return schema_notice_ack ();

  /* News */
  else if (strcmp (key, "news.read") == 0)
    return schema_news_read ();

  /* Subscribe */
  else if (strcmp (key, "subscribe.add") == 0)
    return schema_subscribe_add ();
  else if (strcmp (key, "subscribe.remove") == 0)
    return schema_subscribe_remove ();
  else if (strcmp (key, "subscribe.list") == 0)
    return schema_subscribe_list ();
  else if (strcmp (key, "subscribe.catalog") == 0)
    return schema_subscribe_catalog ();

  /* Bulk */
  else if (strcmp (key, "bulk.execute") == 0)
    return schema_bulk_execute ();

  /* No match found */
  return NULL;
}

json_t *
schema_keys (void)
{
  json_t *keys = json_array ();

  /* Core */
  json_array_append_new (keys, json_string ("envelope"));

  /* Auth */
  json_array_append_new (keys, json_string ("auth.login"));
  json_array_append_new (keys, json_string ("auth.register"));
  json_array_append_new (keys, json_string ("auth.logout"));
  json_array_append_new (keys, json_string ("auth.refresh"));
  json_array_append_new (keys, json_string ("auth.mfa.totp.verify"));

  /* System */
  json_array_append_new (keys, json_string ("system.capabilities"));
  json_array_append_new (keys, json_string ("system.describe_schema"));
  json_array_append_new (keys, json_string ("system.hello"));
  json_array_append_new (keys, json_string ("system.disconnect"));

  /* Session */
  json_array_append_new (keys, json_string ("session.ping"));
  json_array_append_new (keys, json_string ("session.hello"));
  json_array_append_new (keys, json_string ("session.disconnect"));

  /* Ship */
  json_array_append_new (keys, json_string ("ship.inspect"));
  json_array_append_new (keys, json_string ("ship.rename"));
  json_array_append_new (keys, json_string ("ship.reregister"));
  json_array_append_new (keys, json_string ("ship.claim"));
  json_array_append_new (keys, json_string ("ship.status"));
  json_array_append_new (keys, json_string ("ship.info"));
  json_array_append_new (keys, json_string ("ship.transfer_cargo"));
  json_array_append_new (keys, json_string ("ship.jettison"));
  json_array_append_new (keys, json_string ("ship.upgrade"));
  json_array_append_new (keys, json_string ("ship.repair"));
  json_array_append_new (keys, json_string ("ship.self_destruct"));

  /* Port */
  json_array_append_new (keys, json_string ("port.info"));
  json_array_append_new (keys, json_string ("port.status"));
  json_array_append_new (keys, json_string ("port.describe"));

  /* Trade */
  json_array_append_new (keys, json_string ("trade.port_info"));
  json_array_append_new (keys, json_string ("trade.buy"));
  json_array_append_new (keys, json_string ("trade.sell"));
  json_array_append_new (keys, json_string ("trade.quote"));
  json_array_append_new (keys, json_string ("trade.jettison"));
  json_array_append_new (keys, json_string ("trade.offer"));
  json_array_append_new (keys, json_string ("trade.accept"));
  json_array_append_new (keys, json_string ("trade.cancel"));
  json_array_append_new (keys, json_string ("trade.history"));

  /* Move */
  json_array_append_new (keys, json_string ("move.describe_sector"));
  json_array_append_new (keys, json_string ("move.scan"));
  json_array_append_new (keys, json_string ("move.warp"));
  json_array_append_new (keys, json_string ("move.pathfind"));
  json_array_append_new (keys, json_string ("move.autopilot.start"));
  json_array_append_new (keys, json_string ("move.autopilot.stop"));
  json_array_append_new (keys, json_string ("move.autopilot.status"));

  /* Sector */
  json_array_append_new (keys, json_string ("sector.info"));
  json_array_append_new (keys, json_string ("sector.search"));
  json_array_append_new (keys, json_string ("sector.set_beacon"));
  json_array_append_new (keys, json_string ("sector.scan.density"));
  json_array_append_new (keys, json_string ("sector.scan"));

  /* Planet */
  json_array_append_new (keys, json_string ("planet.genesis"));
  json_array_append_new (keys, json_string ("planet.info"));
  json_array_append_new (keys, json_string ("planet.rename"));
  json_array_append_new (keys, json_string ("planet.land"));
  json_array_append_new (keys, json_string ("planet.launch"));
  json_array_append_new (keys, json_string ("planet.transfer_ownership"));
  json_array_append_new (keys, json_string ("planet.harvest"));
  json_array_append_new (keys, json_string ("planet.deposit"));
  json_array_append_new (keys, json_string ("planet.withdraw"));

  /* Citadel */
  json_array_append_new (keys, json_string ("citadel.build"));
  json_array_append_new (keys, json_string ("citadel.upgrade"));

  /* Combat */
  json_array_append_new (keys, json_string ("combat.attack"));
  json_array_append_new (keys, json_string ("combat.deploy_fighters"));
  json_array_append_new (keys, json_string ("combat.lay_mines"));
  json_array_append_new (keys, json_string ("combat.sweep_mines"));
  json_array_append_new (keys, json_string ("combat.status"));

  /* Deploy */
  json_array_append_new (keys, json_string ("deploy.fighters.list"));
  json_array_append_new (keys, json_string ("deploy.mines.list"));

  /* Chat */
  json_array_append_new (keys, json_string ("chat.send"));
  json_array_append_new (keys, json_string ("chat.broadcast"));
  json_array_append_new (keys, json_string ("chat.history"));

  /* Mail */
  json_array_append_new (keys, json_string ("mail.send"));
  json_array_append_new (keys, json_string ("mail.inbox"));
  json_array_append_new (keys, json_string ("mail.read"));
  json_array_append_new (keys, json_string ("mail.delete"));

  /* Notice */
  json_array_append_new (keys, json_string ("sys.notice.create"));
  json_array_append_new (keys, json_string ("notice.list"));
  json_array_append_new (keys, json_string ("notice.ack"));

  /* News */
  json_array_append_new (keys, json_string ("news.read"));

  /* Subscribe */
  json_array_append_new (keys, json_string ("subscribe.add"));
  json_array_append_new (keys, json_string ("subscribe.remove"));
  json_array_append_new (keys, json_string ("subscribe.list"));
  json_array_append_new (keys, json_string ("subscribe.catalog"));

  /* Bulk */
  json_array_append_new (keys, json_string ("bulk.execute"));

  return keys;
}

json_t *
capabilities_build (void)
{
  /* Keep it simple & static for now; you can wire real versions later */
  return json_pack ("{s:s, s:{s:s, s:s, s:s}, s:o, s:o, s:o}",
		    "server", "twclone/0.1-dev",
		    "protocol", "version", "1.0", "min", "1.0", "max", "1.x",
		    "namespaces", json_pack ("[s,s,s,s,s,s,s,s,s,s,s,s,s,s]",
					     "system", "auth", "player",
					     "move", "trade", "ship",
					     "sector", "combat", "planet",
					     "citadel", "chat", "mail",
					     "subscribe", "bulk"), "limits",
		    json_pack ("{s:i, s:i}", "max_frame_bytes", 65536,
			       "max_bulk", 50), "features",
		    json_pack ("{s:b, s:b, s:b, s:b, s:b}", "subscriptions",
			       1, "bulk", 1, "partial", 1, "idempotency", 1,
			       "schemas", 1));
}

/*
 * =============================================================================
 * --- PUBLIC API: C2S Schema Validation ---
 * =============================================================================
 */



int
schema_validate_payload (const char *type, json_t *payload, char **why)
{
  if (why)
    *why = NULL;

  if (!type || !*type)
    {
      if (why)
        *why = why_dup ("command type missing");
      return -1;
    }
  if (!payload || !json_is_object (payload))
    {
      if (why)
        *why = why_dup ("payload must be an object");
      return -1;
    }

  /* 1. GET THE SCHEMA from your registry */
  json_t *schema = schema_get (type);
  if (!schema)
    {
      if (why)
        *why = why_dup ("Unknown command type");
      return -1;
    }

  /*
   * ===================================================================
   * === ADD YOUR DEBUG CODE HERE ===
   * We only want to print if the type matches
   */
    /* Use the 'payload' variable, which exists in this function */
    char *dump = json_dumps(payload, 0);
    fprintf(stderr, "[VALIDATOR] Checking 'sector.set_beacon' with payload: %s\n", dump ? dump : "(null)");
    free(dump);

  /* ===================================================================
   */

  /* 2. VALIDATE THE PAYLOAD against the schema */
  int result = my_json_schema_validate_placeholder (schema, payload, why);

  /*
   * schema_get() returns a new reference (from json_pack),
   * so we MUST decref it when we are done.
   */
  json_decref (schema);

  return result;
}

/*
 * =============================================================================
 * --- PUBLIC API: S2S Manual Validation ---
 * =============================================================================
 */

int
s2s_validate_payload (const char *type, json_t *payload, char **why)
{
  if (why)
    *why = NULL;

  if (!type || !*type)
    {
      if (why)
	*why = why_dup ("type missing");
      return -1;
    }
  if (!payload || !json_is_object (payload))
    {
      if (why)
	*why = why_dup ("payload not object");
      return -1;
    }

  /* --- s2s.health.check --- */
  if (strcmp (type, "s2s.health.check") == 0)
    {
      /* empty or object is fine */
      return 0;
    }

  /* --- s2s.broadcast.sweep --- */
  if (strcmp (type, "s2s.broadcast.sweep") == 0)
    {
      json_t *since = json_object_get (payload, "since_ts");
      if (!since || !json_is_integer (since))
	{
	  if (why)
	    *why = why_dup ("since_ts");
	  return -1;
	}
      /* Optional: filters or page_size ints later */
      return 0;
    }

  /* --- s2s.health.ack --- */
  if (strcmp (type, "s2s.health.ack") == 0)
    {
      if (!json_is_string (json_object_get (payload, "role")))
	{
	  if (why)
	    *why = why_dup ("role");
	  return -1;
	}
      if (!json_is_string (json_object_get (payload, "version")))
	{
	  if (why)
	    *why = why_dup ("version");
	  return -1;
	}
      if (!json_is_integer (json_object_get (payload, "uptime_s")))
	{
	  if (why)
	    *why = why_dup ("uptime_s");
	  return -1;
	}
      return 0;
    }

  /* --- s2s.command.push --- */
  if (strcmp (type, "s2s.command.push") == 0)
    {
      if (!json_is_string (json_object_get (payload, "cmd_type")))
	{
	  if (why)
	    *why = why_dup ("cmd_type");
	  return -1;
	}
      if (!json_is_string (json_object_get (payload, "idem_key")))
	{
	  if (why)
	    *why = why_dup ("idem_key");
	  return -1;
	}
      json_t *pl = json_object_get (payload, "payload");
      if (!pl || !json_is_object (pl))
	{
	  if (why)
	    *why = why_dup ("payload");
	  return -1;
	}
      /* optional: correlation_id (string), priority (int), due_at (int) */
      json_t *cid = json_object_get (payload, "correlation_id");
      if (cid && !json_is_string (cid))
	{
	  if (why)
	    *why = why_dup ("correlation_id");
	  return -1;
	}
      json_t *prio = json_object_get (payload, "priority");
      if (prio && !json_is_integer (prio))
	{
	  if (why)
	    *why = why_dup ("priority");
	  return -1;
	}
      json_t *due = json_object_get (payload, "due_at");
      if (due && !json_is_integer (due))
	{
	  if (why)
	    *why = why_dup ("due_at");
	  return -1;
	}
      return 0;
    }

  /* Unknown types: let dispatcher decide; treat as OK here. */
  return 0;
}

/*
 * =============================================================================
 * --- STATIC SCHEMA IMPLEMENTATIONS (C2S) ---
 *
 * All the stub functions and real schema definitions go here.
 * =============================================================================
 */

/* ----- Implemented Schemas ----- */

static json_t *
schema_envelope (void)
{
  return json_pack ("{s:s, s:s, s:s, s:{s:s}, s:s, s:o, s:o, s:o, s:o}", "$id", "ge://schema/envelope.json", "$schema", "https://json-schema.org/draft/2020-12/schema", "type", "object", "properties", "ts", "string", "oneOf", "[]",	/* keep super-minimal */
		    "required", json_pack ("[s]", "ts"),
		    "additionalProperties", json_true (),
		    "description",
		    "Minimal envelope (server validates more internally)");
}

static json_t *
schema_auth_login (void)
{
  return json_pack ("{s:s, s:s, s:s, s:{s:o}, s:o}",
		    "$id", "ge://schema/auth.login.json",
		    "$schema", "https://json-schema.org/draft/2020-12/schema",
		    "type", "object",
		    "properties", "data", json_pack ("{s:{s:s, s:s}}",
						     "properties",
						     "user_name", "string",
						     "password", "string"),
		    "required", json_pack ("[s,s]", "command", "data"));
}




static json_t *
schema_trade_buy (void)
{
  return json_pack ("{s:s, s:s, s:s, s:{s:o}, s:o}",
		    "$id", "ge://schema/trade.buy.json",
		    "$schema", "https://json-schema.org/draft/2020-12/schema",
		    "type", "object",
		    "properties", "data", json_pack ("{s:{s:s, s:s, s:s}}",
						     "properties",
						     "port_id", "integer",
						     "commodity", "string",
						     "quantity", "integer"),
		    "required", json_pack ("[s,s]", "command", "data"));
}

static json_t *
schema_sector_set_beacon(void)
{
  /*
   * This schema MUST describe the 'data' block itself.
   * The server validator is receiving: {"sector_id": 155, "text": "TESTING!"}
   * This schema validates exactly that.
   */

  // 1. Define properties of the 'data' block
  json_t *data_props = json_pack(
      "{s:{s:s}, s:{s:s}}",
      "sector_id", "type", "integer",
      "text",      "type", "string");

  // 2. Define the schema *for the data block*
  json_t *data_schema = json_pack(
      "{s:s, s:s, s:s, s:o, s:[s,s], s:b}",
      "$id",      "ge://schema/sector.set_beacon.json",
      "$schema",  "https://json-schema.org/draft/2020-12/schema",
      "type",     "object",
      "properties", data_props,
      "required", "sector_id", "text",
      "additionalProperties", 0); // 0 = json_false()

  // 3. Clean up and return the schema for the 'data' block
  json_decref(data_props);
  return data_schema;
}



/* ----- Stub Schemas ----- */

/* --- Auth --- */

static json_t *
schema_auth_register (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/auth.register.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_auth_logout (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/auth.logout.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_auth_refresh (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/auth.refresh.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_auth_mfa_totp_verify (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/auth.mfa.totp.verify.json",
                    "$comment", "Schema not yet implemented");
}

/* --- System --- */

static json_t *
schema_system_capabilities (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/system.capabilities.json",
                    "$comment", "Schema not yet implemented");
}



static json_t *
schema_move_warp (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/move_warp.json",
                    "$comment", "Schema not yet implemented");
}


static json_t *
schema_system_describe_schema (void)
{
  return json_pack (
      "{s:s, s:s, s:s, s:{s:o, s:o}, s:o}",
      "$id", "ge://schema/system.describe_schema.json", 
      "$schema", "https://json-schema.org/draft/2020-12/schema",
      "type", "object", 
      "properties", "data",
      json_pack ("{s:{s:{s:s}}, s:[s]}",
                 "properties", "name", "{s:s}", "type", "string", 
                 "required", "name"),
      "required", json_pack ("[s,s]", "command", "data"));
}

static json_t *
schema_system_hello (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/system.hello.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_system_disconnect (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/system.disconnect.json",
                    "$comment", "Schema not yet implemented");
}

/* --- Session --- */

static json_t *
schema_session_ping (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/session.ping.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_session_hello (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/session.hello.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_session_disconnect (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/session.disconnect.json",
                    "$comment", "Schema not yet implemented");
}

/* --- Ship --- */

static json_t *
schema_ship_inspect (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/ship.inspect.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_ship_rename (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/ship.rename.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_ship_reregister (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/ship.reregister.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_ship_claim (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/ship.claim.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_ship_status (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/ship.status.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_ship_info (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/ship.info.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_ship_transfer_cargo (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/ship.transfer_cargo.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_ship_jettison (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/ship.jettison.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_ship_upgrade (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/ship.upgrade.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_ship_repair (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/ship.repair.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_ship_self_destruct (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/ship.self_destruct.json",
                    "$comment", "Schema not yet implemented");
}

/* --- Port --- */

static json_t *
schema_port_info (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/port.info.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_port_status (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/port.status.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_port_describe (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/port.describe.json",
                    "$comment", "Schema not yet implemented");
}

/* --- Trade --- */

static json_t *
schema_trade_port_info (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/trade.port_info.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_trade_sell (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/trade.sell.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_trade_quote (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/trade.quote.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_trade_jettison (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/trade.jettison.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_trade_offer (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/trade.offer.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_trade_accept (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/trade.accept.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_trade_cancel (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/trade.cancel.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_trade_history (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/trade.history.json",
                    "$comment", "Schema not yet implemented");
}

/* --- Move --- */

static json_t *
schema_move_describe_sector (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/move.describe_sector.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_move_scan (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/move.scan.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_move_pathfind (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/move.pathfind.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_move_autopilot_start (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/move.autopilot.start.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_move_autopilot_stop (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/move.autopilot.stop.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_move_autopilot_status (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/move.autopilot.status.json",
                    "$comment", "Schema not yet implemented");
}

/* --- Sector --- */

static json_t *
schema_sector_info (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/sector.info.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_sector_search (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/sector.search.json",
                    "$comment", "Schema not yet implemented");
}




/* static json_t * */
/* schema_sector_set_beacon (void) */
/* { */
/*   /\* TODO: Implement this schema *\/ */
/*   return json_pack ("{s:s, s:s}", */
/*                     "$id", "ge://schema/sector.set_beacon.json", */
/*                     "$comment", "Schema not yet implemented"); */
/* } */

static json_t *
schema_sector_scan_density (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/sector.scan.density.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_sector_scan (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/sector.scan.json",
                    "$comment", "Schema not yet implemented");
}

/* --- Planet --- */

static json_t *
schema_planet_genesis (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/planet.genesis.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_planet_info (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/planet.info.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_planet_rename (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/planet.rename.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_planet_land (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/planet.land.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_planet_launch (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/planet.launch.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_planet_transfer_ownership (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/planet.transfer_ownership.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_planet_harvest (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/planet.harvest.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_planet_deposit (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/planet.deposit.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_planet_withdraw (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/planet.withdraw.json",
                    "$comment", "Schema not yet implemented");
}

/* --- Citadel --- */

static json_t *
schema_citadel_build (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/citadel.build.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_citadel_upgrade (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/citadel.upgrade.json",
                    "$comment", "Schema not yet implemented");
}

/* --- Combat --- */

static json_t *
schema_combat_attack (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/combat.attack.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_combat_deploy_fighters (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/combat.deploy_fighters.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_combat_lay_mines (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/combat.lay_mines.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_combat_sweep_mines (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/combat.sweep_mines.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_combat_status (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/combat.status.json",
                    "$comment", "Schema not yet implemented");
}

/* --- Deploy --- */

static json_t *
schema_deploy_fighters_list (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/deploy.fighters.list.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_deploy_mines_list (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/deploy.mines.list.json",
                    "$comment", "Schema not yet implemented");
}

/* --- Chat --- */

static json_t *
schema_chat_send (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/chat.send.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_chat_broadcast (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/chat.broadcast.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_chat_history (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/chat.history.json",
                    "$comment", "Schema not yet implemented");
}

/* --- Mail --- */

static json_t *
schema_mail_send (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/mail.send.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_mail_inbox (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/mail.inbox.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_mail_read (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/mail.read.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_mail_delete (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/mail.delete.json",
                    "$comment", "Schema not yet implemented");
}

/* --- Notice --- */

static json_t *
schema_sys_notice_create (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/sys.notice.create.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_notice_list (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/notice.list.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_notice_ack (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/notice.ack.json",
                    "$comment", "Schema not yet implemented");
}

/* --- News --- */

static json_t *
schema_news_read (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/news.read.json",
                    "$comment", "Schema not yet implemented");
}

/* --- Subscribe --- */

static json_t *
schema_subscribe_add (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/subscribe.add.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_subscribe_remove (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/subscribe.remove.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_subscribe_list (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/subscribe.list.json",
                    "$comment", "Schema not yet implemented");
}

static json_t *
schema_subscribe_catalog (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/subscribe.catalog.json",
                    "$comment", "Schema not yet implemented");
}

/* --- Bulk --- */

static json_t *
schema_bulk_execute (void)
{
  /* TODO: Implement this schema */
  return json_pack ("{s:s, s:s}",
                    "$id", "ge://schema/bulk.execute.json",
                    "$comment", "Schema not yet implemented");
}
