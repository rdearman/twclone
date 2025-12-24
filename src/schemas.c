#include <jansson.h>
#include <stdbool.h>
#include "schemas.h"
#include <stdlib.h>
#include "server_log.h"
#include "server_config.h"


/*
 * =============================================================================
 * --- Forward Declarations ---
 *
 * This fixes the compilation error where schema_get() was called
 * before its static functions were defined.
 * =============================================================================
 */


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
    {
      memcpy (p, m, n);
    }
  return p;
}


/**
 * @brief Placeholder for a real JSON Schema validation function.
 *
 * You will need to replace this with a real validation library
 * (e.g., libjsonschema) to perform actual schema validation.
 */
static int
my_json_schema_validate_placeholder (json_t *schema, json_t *payload,
                                     char **why)
{
  /* TODO(GH-391): Implement this using a real validator library. */
  (void) schema;                /* Suppress unused parameter warning */
  (void) payload;               /* Suppress unused parameter warning */
  /* For now, we just pretend it's always valid */
  if (why)
    {
      *why = NULL;
    }
  return 0;
}


/*
 * =============================================================================
 * --- PUBLIC API: C2S Schema Registry ---
 * =============================================================================
 */

#include <pthread.h>

static pthread_mutex_t g_schema_mu = PTHREAD_MUTEX_INITIALIZER;


typedef struct schema_entry
{
  const char *key;
  json_t *schema;               /* cache owns this ref */


  json_t *(*builder) (void);
} schema_entry_t;

/* Define the builders locally as needed or via externs */
extern json_t *schema_envelope (void);
extern json_t *schema_auth_login (void);
extern json_t *schema_auth_register (void);
extern json_t *schema_auth_logout (void);
extern json_t *schema_auth_refresh (void);
extern json_t *schema_auth_mfa_totp_verify (void);
extern json_t *schema_admin_notice (void);
extern json_t *schema_admin_shutdown_warning (void);
extern json_t *schema_system_capabilities (void);
extern json_t *schema_system_describe_schema (void);
extern json_t *schema_system_hello (void);
extern json_t *schema_system_disconnect (void);
extern json_t *schema_session_ping (void);
extern json_t *schema_session_hello (void);
extern json_t *schema_session_disconnect (void);
extern json_t *schema_ship_inspect (void);
extern json_t *schema_ship_rename (void);
extern json_t *schema_ship_reregister (void);
extern json_t *schema_ship_claim (void);
extern json_t *schema_ship_status (void);
extern json_t *schema_ship_info (void);
extern json_t *schema_ship_transfer_cargo (void);
extern json_t *schema_ship_jettison (void);
extern json_t *schema_ship_upgrade (void);
extern json_t *schema_ship_repair (void);
extern json_t *schema_ship_self_destruct (void);
extern json_t *schema_hardware_list (void);
extern json_t *schema_hardware_buy (void);
extern json_t *schema_port_info (void);
extern json_t *schema_port_status (void);
extern json_t *schema_port_describe (void);
extern json_t *schema_port_rob (void);
extern json_t *schema_trade_port_info (void);
extern json_t *schema_trade_buy (void);
extern json_t *schema_trade_sell (void);
extern json_t *schema_trade_quote (void);
extern json_t *schema_trade_jettison (void);
extern json_t *schema_trade_offer (void);
extern json_t *schema_trade_accept (void);
extern json_t *schema_trade_cancel (void);
extern json_t *schema_trade_history (void);
extern json_t *schema_move_describe_sector (void);
extern json_t *schema_move_scan (void);
extern json_t *schema_move_warp (void);
extern json_t *schema_move_pathfind (void);
extern json_t *schema_move_autopilot_start (void);
extern json_t *schema_move_autopilot_stop (void);
extern json_t *schema_move_autopilot_status (void);
extern json_t *schema_sector_info (void);
extern json_t *schema_sector_search (void);
extern json_t *schema_sector_set_beacon (void);
extern json_t *schema_sector_scan_density (void);
extern json_t *schema_sector_scan (void);
extern json_t *schema_planet_genesis (void);
extern json_t *schema_planet_info (void);
extern json_t *schema_planet_rename (void);
extern json_t *schema_planet_land (void);
extern json_t *schema_planet_launch (void);
extern json_t *schema_planet_transfer_ownership (void);
extern json_t *schema_planet_harvest (void);
extern json_t *schema_planet_deposit (void);
extern json_t *schema_planet_withdraw (void);
extern json_t *schema_planet_genesis_create (void);
extern json_t *schema_player_set_trade_account_preference (void);
extern json_t *schema_player_my_info (void);
extern json_t *schema_player_info (void);
extern json_t *schema_bank_balance (void);
extern json_t *schema_bank_history (void);
extern json_t *schema_bank_leaderboard (void);
extern json_t *schema_player_list_online_request (void);
extern json_t *schema_player_list_online_response (void);
extern json_t *schema_port_update (void);
extern json_t *schema_citadel_build (void);
extern json_t *schema_citadel_upgrade (void);
extern json_t *schema_combat_attack (void);
extern json_t *schema_combat_deploy_fighters (void);
extern json_t *schema_combat_lay_mines (void);
extern json_t *schema_combat_sweep_mines (void);
extern json_t *schema_combat_status (void);
extern json_t *schema_fighters_recall (void);
extern json_t *schema_combat_deploy_mines (void);
extern json_t *schema_mines_recall (void);
extern json_t *schema_deploy_fighters_list (void);
extern json_t *schema_deploy_mines_list (void);
extern json_t *schema_chat_send (void);
extern json_t *schema_chat_broadcast (void);
extern json_t *schema_chat_history (void);
extern json_t *schema_mail_send (void);
extern json_t *schema_mail_inbox (void);
extern json_t *schema_mail_read (void);
extern json_t *schema_mail_delete (void);
extern json_t *schema_sys_notice_create (void);
extern json_t *schema_notice_list (void);
extern json_t *schema_notice_ack (void);
extern json_t *schema_insurance_policies_list (void);
extern json_t *schema_insurance_policies_buy (void);
extern json_t *schema_insurance_claim_file (void);
extern json_t *schema_fine_list (void);
extern json_t *schema_fine_pay (void);
extern json_t *schema_news_read (void);
extern json_t *schema_subscribe_add (void);
extern json_t *schema_subscribe_remove (void);
extern json_t *schema_subscribe_list (void);
extern json_t *schema_subscribe_catalog (void);
extern json_t *schema_bulk_execute (void);

static schema_entry_t g_schema_table[] = {
  {"envelope", NULL, schema_envelope},
  {"auth.login", NULL, schema_auth_login},
  {"auth.register", NULL, schema_auth_register},
  {"auth.logout", NULL, schema_auth_logout},
  {"auth.refresh", NULL, schema_auth_refresh},
  {"auth.mfa_totp_verify", NULL, schema_auth_mfa_totp_verify},
  {"admin.notice", NULL, schema_admin_notice},
  {"admin.shutdown_warning", NULL, schema_admin_shutdown_warning},
  {"system.capabilities", NULL, schema_system_capabilities},
  {"system.describe_schema", NULL, schema_system_describe_schema},
  {"system.hello", NULL, schema_system_hello},
  {"system.disconnect", NULL, schema_system_disconnect},
  {"session.ping", NULL, schema_session_ping},
  {"session.hello", NULL, schema_session_hello},
  {"session.disconnect", NULL, schema_session_disconnect},
  {"ship.inspect", NULL, schema_ship_inspect},
  {"ship.rename", NULL, schema_ship_rename},
  {"ship.reregister", NULL, schema_ship_reregister},
  {"ship.claim", NULL, schema_ship_claim},
  {"ship.status", NULL, schema_ship_status},
  {"ship.info", NULL, schema_ship_info},
  {"ship.transfer_cargo", NULL, schema_ship_transfer_cargo},
  {"ship.jettison", NULL, schema_ship_jettison},
  {"ship.upgrade", NULL, schema_ship_upgrade},
  {"ship.repair", NULL, schema_ship_repair},
  {"ship.self_destruct", NULL, schema_ship_self_destruct},
  {"hardware.list", NULL, schema_hardware_list},
  {"hardware.buy", NULL, schema_hardware_buy},
  {"port.info", NULL, schema_port_info},
  {"port.status", NULL, schema_port_status},
  {"port.describe", NULL, schema_port_describe},
  {"port.rob", NULL, schema_port_rob},
  {"trade.port_info", NULL, schema_trade_port_info},
  {"trade.buy", NULL, schema_trade_buy},
  {"trade.sell", NULL, schema_trade_sell},
  {"trade.quote", NULL, schema_trade_quote},
  {"trade.jettison", NULL, schema_trade_jettison},
  {"trade.offer", NULL, schema_trade_offer},
  {"trade.accept", NULL, schema_trade_accept},
  {"trade.cancel", NULL, schema_trade_cancel},
  {"trade.history", NULL, schema_trade_history},
  {"move.describe_sector", NULL, schema_move_describe_sector},
  {"move.scan", NULL, schema_move_scan},
  {"move.warp", NULL, schema_move_warp},
  {"move.pathfind", NULL, schema_move_pathfind},
  {"move.autopilot_start", NULL, schema_move_autopilot_start},
  {"move.autopilot_stop", NULL, schema_move_autopilot_stop},
  {"move.autopilot_status", NULL, schema_move_autopilot_status},
  {"sector.info", NULL, schema_sector_info},
  {"sector.search", NULL, schema_sector_search},
  {"sector.set_beacon", NULL, schema_sector_set_beacon},
  {"sector.scan_density", NULL, schema_sector_scan_density},
  {"sector.scan", NULL, schema_sector_scan},
  {"planet.genesis", NULL, schema_planet_genesis},
  {"planet.info", NULL, schema_planet_info},
  {"planet.rename", NULL, schema_planet_rename},
  {"planet.land", NULL, schema_planet_land},
  {"planet.launch", NULL, schema_planet_launch},
  {"planet.transfer_ownership", NULL, schema_planet_transfer_ownership},
  {"planet.harvest", NULL, schema_planet_harvest},
  {"planet.deposit", NULL, schema_planet_deposit},
  {"planet.withdraw", NULL, schema_planet_withdraw},
  {"planet.genesis_create", NULL, schema_planet_genesis_create},
  {"player.set_trade_account_preference", NULL,
   schema_player_set_trade_account_preference},
  {"player.my_info", NULL, schema_player_my_info},
  {"player.info", NULL, schema_player_info},
  {"bank.balance", NULL, schema_bank_balance},
  {"bank.history", NULL, schema_bank_history},
  {"bank.leaderboard", NULL, schema_bank_leaderboard},
  {"player.list_online_request", NULL, schema_player_list_online_request},
  {"player.list_online_response", NULL, schema_player_list_online_response},
  {"port.update", NULL, schema_port_update},
  {"citadel.build", NULL, schema_citadel_build},
  {"citadel.upgrade", NULL, schema_citadel_upgrade},
  {"combat.attack", NULL, schema_combat_attack},
  {"combat.deploy_fighters", NULL, schema_combat_deploy_fighters},
  {"combat.lay_mines", NULL, schema_combat_lay_mines},
  {"combat.sweep_mines", NULL, schema_combat_sweep_mines},
  {"combat.status", NULL, schema_combat_status},
  {"fighters.recall", NULL, schema_fighters_recall},
  {"combat.deploy_mines", NULL, schema_combat_deploy_mines},
  {"mines.recall", NULL, schema_mines_recall},
  {"deploy.fighters.list", NULL, schema_deploy_fighters_list},
  {"deploy.mines.list", NULL, schema_deploy_mines_list},
  {"chat.send", NULL, schema_chat_send},
  {"chat.broadcast", NULL, schema_chat_broadcast},
  {"chat.history", NULL, schema_chat_history},
  {"mail.send", NULL, schema_mail_send},
  {"mail.inbox", NULL, schema_mail_inbox},
  {"mail.read", NULL, schema_mail_read},
  {"mail.delete", NULL, schema_mail_delete},
  {"sys.notice.create", NULL, schema_sys_notice_create},
  {"notice.list", NULL, schema_notice_list},
  {"notice.ack", NULL, schema_notice_ack},
  {"insurance.policies_list", NULL, schema_insurance_policies_list},
  {"insurance.policies_buy", NULL, schema_insurance_policies_buy},
  {"insurance.claim_file", NULL, schema_insurance_claim_file},
  {"fine.list", NULL, schema_fine_list},
  {"fine.pay", NULL, schema_fine_pay},
  {"news.read", NULL, schema_news_read},
  {"subscribe.add", NULL, schema_subscribe_add},
  {"subscribe.remove", NULL, schema_subscribe_remove},
  {"subscribe.list", NULL, schema_subscribe_list},
  {"subscribe.catalog", NULL, schema_subscribe_catalog},
  {"bulk.execute", NULL, schema_bulk_execute},
};


json_t *
schema_placeholder (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/placeholder.json"));
  json_object_set_new (root, "$comment", json_string ("Not yet implemented"));
  json_object_set_new (root, "type", json_string ("object"));
  return root;
}


json_t *
schema_get (const char *key)
{
  if (!key)
    {
      return NULL;
    }

  pthread_mutex_lock (&g_schema_mu);

  for (size_t i = 0; i < sizeof (g_schema_table) / sizeof (g_schema_table[0]);
       i++)
    {
      schema_entry_t *e = &g_schema_table[i];


      if (strcasecmp (e->key, key) == 0)
        {
          if (!e->schema)
            {
              if (e->builder)
                {
                  e->schema = e->builder ();
                }
            }
          json_t *out = e->schema ? json_incref (e->schema) : NULL;


          pthread_mutex_unlock (&g_schema_mu);
          return out;
        }
    }

  pthread_mutex_unlock (&g_schema_mu);

  /* Fallback to registry if not in our table */
  return loop_get_schema_for_command (key);
}


void
schema_shutdown (void)
{
  pthread_mutex_lock (&g_schema_mu);
  for (size_t i = 0; i < sizeof (g_schema_table) / sizeof (g_schema_table[0]);
       i++)
    {
      if (g_schema_table[i].schema)
        {
          json_decref (g_schema_table[i].schema);
          g_schema_table[i].schema = NULL;
        }
    }
  pthread_mutex_unlock (&g_schema_mu);
}


json_t *
schema_keys (void)
{
  return loop_get_all_schema_keys ();
}


json_t *
capabilities_build (void)
{
  /* Keep it simple & static for now; you can wire real versions later */
  json_t *root = json_object ();
  json_object_set_new (root, "server", json_string ("twclone/0.1-dev"));


  json_t *protocol = json_object ();


  json_object_set_new (protocol, "version", json_string ("1.0"));
  json_object_set_new (protocol, "min", json_string ("1.0"));
  json_object_set_new (protocol, "max", json_string ("1.x"));
  json_object_set_new (root, "protocol", protocol);


  json_t *namespaces = json_array ();


  json_array_append_new (namespaces, json_string ("system"));
  json_array_append_new (namespaces, json_string ("auth"));
  json_array_append_new (namespaces, json_string ("player"));
  json_array_append_new (namespaces, json_string ("move"));
  json_array_append_new (namespaces, json_string ("trade"));
  json_array_append_new (namespaces, json_string ("ship"));
  json_array_append_new (namespaces, json_string ("sector"));
  json_array_append_new (namespaces, json_string ("combat"));
  json_array_append_new (namespaces, json_string ("planet"));
  json_array_append_new (namespaces, json_string ("citadel"));
  json_array_append_new (namespaces, json_string ("chat"));
  json_array_append_new (namespaces, json_string ("mail"));
  json_array_append_new (namespaces, json_string ("subscribe"));
  json_array_append_new (namespaces, json_string ("bulk"));
  json_object_set_new (root, "namespaces", namespaces);


  json_t *limits = json_object ();


  json_object_set_new (limits, "max_frame_bytes", json_integer (65536));
  json_object_set_new (limits, "max_bulk", json_integer (50));
  json_object_set_new (root, "limits", limits);


  json_t *features = json_object ();


  json_object_set_new (features, "subscriptions", json_boolean (1));
  json_object_set_new (features, "bulk", json_boolean (1));
  json_object_set_new (features, "partial", json_boolean (1));
  json_object_set_new (features, "idempotency", json_boolean (1));
  json_object_set_new (features, "schemas", json_boolean (1));
  json_object_set_new (root, "features", features);


  return root;
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
    {
      *why = NULL;
    }
  if (!type || !*type)
    {
      if (why)
        {
          *why = why_dup ("command type missing");
        }
      return -1;
    }
  if (!payload || !json_is_object (payload))
    {
      if (why)
        {
          *why = why_dup ("payload must be an object");
        }
      return -1;
    }
  /* 1. GET THE SCHEMA from your registry */
  json_t *schema = schema_get (type);


  if (!schema)
    {
      if (why)
        {
          *why = why_dup ("Unknown command type");
        }
      return -1;
    }

  /*
   * ===================================================================
   * === ADD YOUR DEBUG CODE HERE ===
   * We only want to print if the type matches
   */
  /* Use the 'payload' variable, which exists in this function */
  char *dump = json_dumps (payload, 0);


  LOGD ("[VALIDATOR] Checking 'sector.set_beacon' with payload: %s",
        dump ? dump : "(null)");
  free (dump);

  /* ===================================================================
   */
  /* 2. VALIDATE THE PAYLOAD against the schema */
  int result = my_json_schema_validate_placeholder (schema, payload, why);


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
    {
      *why = NULL;
    }
  if (!type || !*type)
    {
      if (why)
        {
          *why = why_dup ("type missing");
        }
      return -1;
    }
  if (!payload || !json_is_object (payload))
    {
      if (why)
        {
          *why = why_dup ("payload not object");
        }
      return -1;
    }
  /* --- s2s.health.check --- */
  if (strcasecmp (type, "s2s.health.check") == 0)
    {
      /* empty or object is fine */
      return 0;
    }
  /* --- s2s.broadcast.sweep --- */
  if (strcasecmp (type, "s2s.broadcast.sweep") == 0)
    {
      json_t *since = json_object_get (payload, "since_ts");


      if (!since || !json_is_integer (since))
        {
          if (why)
            {
              *why = why_dup ("since_ts");
            }
          return -1;
        }
      /* Optional: filters or page_size ints later */
      return 0;
    }
  /* --- s2s.health.ack --- */
  if (strcasecmp (type, "s2s.health.ack") == 0)
    {
      if (!json_is_string (json_object_get (payload, "role")))
        {
          if (why)
            {
              *why = why_dup ("role");
            }
          return -1;
        }
      if (!json_is_string (json_object_get (payload, "version")))
        {
          if (why)
            {
              *why = why_dup ("version");
            }
          return -1;
        }
      if (!json_is_integer (json_object_get (payload, "uptime_s")))
        {
          if (why)
            {
              *why = why_dup ("uptime_s");
            }
          return -1;
        }
      return 0;
    }
  /* --- s2s.command.push --- */
  if (strcasecmp (type, "s2s.command.push") == 0)
    {
      if (!json_is_string (json_object_get (payload, "cmd_type")))
        {
          if (why)
            {
              *why = why_dup ("cmd_type");
            }
          return -1;
        }
      if (!json_is_string (json_object_get (payload, "idem_key")))
        {
          if (why)
            {
              *why = why_dup ("idem_key");
            }
          return -1;
        }
      json_t *pl = json_object_get (payload, "payload");


      if (!pl || !json_is_object (pl))
        {
          if (why)
            {
              *why = why_dup ("payload");
            }
          return -1;
        }
      /* optional: correlation_id (string), priority (int), due_at (int) */
      json_t *cid = json_object_get (payload, "correlation_id");


      if (cid && !json_is_string (cid))
        {
          if (why)
            {
              *why = why_dup ("correlation_id");
            }
          return -1;
        }
      json_t *prio = json_object_get (payload, "priority");


      if (prio && !json_is_integer (prio))
        {
          if (why)
            {
              *why = why_dup ("priority");
            }
          return -1;
        }
      json_t *due = json_object_get (payload, "due_at");


      if (due && !json_is_integer (due))
        {
          if (why)
            {
              *why = why_dup ("due_at");
            }
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
json_t *
schema_envelope (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/envelope.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));


  json_t *props = json_object ();


  json_object_set_new (props, "ts", json_string ("string"));
  json_object_set_new (root, "properties", props);


  json_object_set_new (root, "oneOf", json_string ("[]"));


  /* keep super-minimal */
  json_t *required = json_array ();


  json_array_append_new (required, json_string ("ts"));
  json_object_set_new (root, "required", required);


  json_object_set_new (root, "additionalProperties", json_boolean (1));
  json_object_set_new (root, "description",
                       json_string
                         ("Minimal envelope (server validates more internally)"));
  return root;
}


json_t *
schema_auth_login (void)
{
  json_t *data_properties = json_object ();
  json_t *username_prop = json_object ();
  json_object_set_new (username_prop, "type", json_string ("string"));
  json_object_set_new (data_properties, "username", username_prop);


  json_t *passwd_prop = json_object ();


  json_object_set_new (passwd_prop, "type", json_string ("string"));
  json_object_set_new (data_properties, "passwd", passwd_prop);


  json_t *data_required = json_array ();


  json_array_append_new (data_required, json_string ("username"));
  json_array_append_new (data_required, json_string ("passwd"));


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/auth.login.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_properties);
  json_object_set_new (data_schema, "required", data_required);
  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));


  return data_schema;
}


json_t *
schema_trade_buy (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/trade.buy.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("port_id"));
  json_array_append_new (required, json_string ("items"));
  json_array_append_new (required, json_string ("idempotency_key"));
  json_array_append_new (required, json_string ("account"));
  json_array_append_new (required, json_string ("sector_id"));
  json_object_set_new (root, "required", required);


  json_object_set_new (root, "additionalProperties", json_boolean (0));


  json_t *props = json_object ();


  json_object_set_new (root, "properties", props);


  json_t *port_id_prop = json_object ();


  json_object_set_new (port_id_prop, "type", json_string ("integer"));
  json_object_set_new (props, "port_id", port_id_prop);


  json_t *idem_key_prop = json_object ();


  json_object_set_new (idem_key_prop, "type", json_string ("string"));
  json_object_set_new (props, "idempotency_key", idem_key_prop);


  json_t *account_prop = json_object ();


  json_object_set_new (account_prop, "type", json_string ("integer"));
  json_t *account_enum = json_array ();


  json_array_append_new (account_enum, json_integer (0));
  json_array_append_new (account_enum, json_integer (1));
  json_object_set_new (account_prop, "enum", account_enum);
  json_object_set_new (props, "account", account_prop);


  json_t *sector_id_prop = json_object ();


  json_object_set_new (sector_id_prop, "type", json_string ("integer"));
  json_object_set_new (props, "sector_id", sector_id_prop);


  JSON_AUTO item_schema = json_object ();


  json_object_set_new (item_schema, "type", json_string ("object"));
  json_t *item_required = json_array ();


  json_array_append_new (item_required, json_string ("commodity"));
  json_array_append_new (item_required, json_string ("quantity"));
  json_object_set_new (item_schema, "required", item_required);
  json_object_set_new (item_schema, "additionalProperties", json_boolean (0));


  json_t *item_props = json_object ();


  json_object_set_new (item_schema, "properties", item_props);


  json_t *comm_prop = json_object ();


  json_object_set_new (comm_prop, "type", json_string ("string"));
  json_object_set_new (item_props, "commodity", comm_prop);


  json_t *qty_prop = json_object ();


  json_object_set_new (qty_prop, "type", json_string ("integer"));
  json_object_set_new (item_props, "quantity", qty_prop);


  json_t *items_prop = json_object ();


  json_object_set_new (items_prop, "type", json_string ("array"));
  json_object_set (items_prop, "items", item_schema);
  json_object_set_new (props, "items", items_prop);


  return root;
}


json_t *
schema_sector_set_beacon (void)
{
  /*
   * This schema MUST describe the 'data' block itself.
   * The server validator is receiving: {"sector_id": 155, "text": "TESTING!"}
   * This schema validates exactly that.
   */
  // 1. Define properties of the 'data' block
  json_t *data_props = json_object ();
  json_t *sector_id_prop = json_object ();
  json_object_set_new (sector_id_prop, "type", json_string ("integer"));
  json_object_set_new (data_props, "sector_id", sector_id_prop);


  json_t *text_prop = json_object ();


  json_object_set_new (text_prop, "type", json_string ("string"));
  json_object_set_new (data_props, "text", text_prop);


  // 2. Define the schema *for the data block*
  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/sector.set_beacon.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_props);


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("sector_id"));
  json_array_append_new (required, json_string ("text"));
  json_object_set_new (data_schema, "required", required);


  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));


  // 3. Clean up and return the schema for the 'data' block
  return data_schema;
}


/* ----- Stub Schemas ----- */


/* --- Auth --- */
json_t *
schema_auth_register (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/auth.register.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("username"));
  json_array_append_new (required, json_string ("passwd"));
  json_object_set_new (root, "required", required);


  json_object_set_new (root, "additionalProperties", json_boolean (0));


  json_t *props = json_object ();


  json_object_set_new (root, "properties", props);


  json_t *user_prop = json_object ();


  json_object_set_new (user_prop, "type", json_string ("string"));
  json_object_set_new (props, "username", user_prop);


  json_t *pass_prop = json_object ();


  json_object_set_new (pass_prop, "type", json_string ("string"));
  json_object_set_new (props, "passwd", pass_prop);


  json_t *ship_prop = json_object ();


  json_object_set_new (ship_prop, "type", json_string ("string"));
  json_object_set_new (props, "ship_name", ship_prop);


  json_t *locale_prop = json_object ();


  json_object_set_new (locale_prop, "type", json_string ("string"));
  json_object_set_new (props, "ui_locale", locale_prop);


  json_t *tz_prop = json_object ();


  json_object_set_new (tz_prop, "type", json_string ("string"));
  json_object_set_new (props, "ui_timezone", tz_prop);


  return root;
}


json_t *
schema_auth_logout (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/auth.logout.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_auth_refresh (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root,
                       "$id", json_string ("ge://schema/auth.refresh.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_auth_mfa_totp_verify (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/auth.mfa.totp.verify.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_admin_notice (void)
{
  json_t *root = json_object ();
  json_object_set_new (root,
                       "$id", json_string ("ge://schema/admin.notice.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));
  return root;
}


json_t *
schema_admin_shutdown_warning (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string
                         ("ge://schema/admin.shutdown_warning.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));
  return root;
}


/* --- System --- */
json_t *
schema_system_capabilities (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/system.capabilities.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_move_warp (void)
{
  json_t *data_props = json_object ();
  json_t *to_sector_id_prop = json_object ();
  json_object_set_new (to_sector_id_prop, "type", json_string ("integer"));
  json_object_set_new (data_props, "to_sector_id", to_sector_id_prop);


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/move_warp.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_props);


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("to_sector_id"));
  json_object_set_new (data_schema, "required", required);


  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));

  return data_schema;
}


json_t *
schema_system_describe_schema (void)
{
  json_t *name_prop = json_object ();
  json_object_set_new (name_prop, "type", json_string ("string"));


  json_t *type_prop = json_object ();


  json_object_set_new (type_prop, "type", json_string ("string"));
  json_t *type_enum = json_array ();


  json_array_append_new (type_enum, json_string ("command"));
  json_array_append_new (type_enum, json_string ("event"));
  json_object_set_new (type_prop, "enum", type_enum);


  json_t *data_properties = json_object ();


  json_object_set_new (data_properties, "name", name_prop);
  json_object_set_new (data_properties, "type", type_prop);


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string
                         ("ge://schema/system.describe_schema.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_properties);


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("name"));
  json_array_append_new (required, json_string ("type"));
  json_object_set_new (data_schema, "required", required);


  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));


  return data_schema;
}


json_t *
schema_system_hello (void)
{
  json_t *root = json_object ();
  json_object_set_new (root,
                       "$id", json_string ("ge://schema/system.hello.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("client_version"));
  json_object_set_new (root, "required", required);


  json_object_set_new (root, "additionalProperties", json_boolean (0));


  json_t *props = json_object ();


  json_object_set_new (root, "properties", props);


  json_t *client_version_prop = json_object ();


  json_object_set_new (client_version_prop, "type", json_string ("string"));
  json_object_set_new (props, "client_version", client_version_prop);


  return root;
}


json_t *
schema_system_disconnect (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/system.disconnect.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


/* --- Session --- */
json_t *
schema_session_ping (void)
{
  json_t *root = json_object ();
  json_object_set_new (root,
                       "$id", json_string ("ge://schema/session.ping.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));
  json_object_set_new (root, "properties", json_object ());
  // Empty properties object
  json_object_set_new (root, "required", json_array ());
  // No required properties
  json_object_set_new (root, "additionalProperties", json_boolean (0));
  return root;
}


json_t *
schema_session_hello (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/session.hello.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));
  json_object_set_new (root, "properties", json_object ());
  // Empty properties object
  json_object_set_new (root, "required", json_array ());
  // No required properties
  json_object_set_new (root, "additionalProperties", json_boolean (0));
  return root;
}


json_t *
schema_session_disconnect (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/session.disconnect.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


/* --- Ship --- */
json_t *
schema_ship_inspect (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root,
                       "$id", json_string ("ge://schema/ship.inspect.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_ship_rename (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/ship.rename.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_ship_reregister (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/ship.reregister.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_ship_claim (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/ship.claim.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


////
json_t *
schema_ship_status (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/ship.status.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));

  json_t *props = json_object ();


  json_object_set_new (root, "properties", props);

  json_t *ship = json_object ();


  json_object_set_new (props, "ship", ship);
  json_object_set_new (ship, "type", json_string ("object"));

  json_t *ship_props = json_object ();


  json_object_set_new (ship, "properties", ship_props);

  /* simple leaf properties */
  json_t *id_prop = json_object ();


  json_object_set_new (id_prop, "type", json_string ("integer"));
  json_object_set_new (ship_props, "id", id_prop);


  json_t *number_prop = json_object ();


  json_object_set_new (number_prop, "type", json_string ("integer"));
  json_object_set_new (ship_props, "number", number_prop);


  json_t *name_prop = json_object ();


  json_object_set_new (name_prop, "type", json_string ("string"));
  json_object_set_new (ship_props, "name", name_prop);

  /* TODO: add the rest of your properties here, one by one, same style */

  /* required: ["ship"] */
  json_t *required = json_array ();


  json_array_append_new (required, json_string ("ship"));
  json_object_set_new (root, "required", required);

  json_object_set_new (root, "additionalProperties", 0);

  return root;
}


json_t *
schema_ship_info (void)
{
  json_t *s = schema_ship_status ();
  json_object_set_new (s, "$id", json_string ("ge://schema/ship.info.json"));
  return s;
}


json_t *
schema_ship_transfer_cargo (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/ship.transfer_cargo.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_ship_jettison (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/ship.jettison.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_ship_upgrade (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root,
                       "$id", json_string ("ge://schema/ship.upgrade.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_ship_repair (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/ship.repair.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_ship_self_destruct (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/ship.self_destruct.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


/* --- Port --- */
json_t *
schema_port_info (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/port.info.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_port_status (void)
{
  json_t *data_properties = json_object ();
  json_t *port_id_prop = json_object ();
  json_object_set_new (port_id_prop, "type", json_string ("integer"));
  json_object_set_new (data_properties, "port_id", port_id_prop);


  json_t *sector_id_prop = json_object ();


  json_object_set_new (sector_id_prop, "type", json_string ("integer"));
  json_object_set_new (data_properties, "sector_id", sector_id_prop);


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/port.status.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_properties);


  json_t *anyOf = json_array ();


  json_t *opt1 = json_object ();
  json_t *req1 = json_array ();


  json_array_append_new (req1, json_string ("port_id"));
  json_object_set_new (opt1, "required", req1);
  json_array_append_new (anyOf, opt1);


  json_t *opt2 = json_object ();
  json_t *req2 = json_array ();


  json_array_append_new (req2, json_string ("sector_id"));
  json_object_set_new (opt2, "required", req2);
  json_array_append_new (anyOf, opt2);


  json_object_set_new (data_schema, "anyOf", anyOf);
  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));


  return data_schema;
}


json_t *
schema_port_describe (void)
{
  json_t *data_properties = json_object ();
  json_t *port_id_prop = json_object ();
  json_object_set_new (port_id_prop, "type", json_string ("integer"));
  json_object_set_new (data_properties, "port_id", port_id_prop);


  json_t *sector_id_prop = json_object ();


  json_object_set_new (sector_id_prop, "type", json_string ("integer"));
  json_object_set_new (data_properties, "sector_id", sector_id_prop);


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/port.describe.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_properties);


  json_t *anyOf = json_array ();


  json_t *opt1 = json_object ();
  json_t *req1 = json_array ();


  json_array_append_new (req1, json_string ("port_id"));
  json_object_set_new (opt1, "required", req1);
  json_array_append_new (anyOf, opt1);


  json_t *opt2 = json_object ();
  json_t *req2 = json_array ();


  json_array_append_new (req2, json_string ("sector_id"));
  json_object_set_new (opt2, "required", req2);
  json_array_append_new (anyOf, opt2);


  json_object_set_new (data_schema, "anyOf", anyOf);
  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));


  return data_schema;
}


json_t *
schema_port_rob (void)
{
  json_t *data_properties = json_object ();


  json_t *sector_id_prop = json_object ();
  json_object_set_new (sector_id_prop, "type", json_string ("integer"));
  json_object_set_new (data_properties, "sector_id", sector_id_prop);


  json_t *port_id_prop = json_object ();


  json_object_set_new (port_id_prop, "type", json_string ("integer"));
  json_object_set_new (data_properties, "port_id", port_id_prop);


  json_t *mode_prop = json_object ();


  json_object_set_new (mode_prop, "type", json_string ("string"));
  json_t *mode_enum = json_array ();


  json_array_append_new (mode_enum, json_string ("credits"));
  json_array_append_new (mode_enum, json_string ("goods"));
  json_object_set_new (mode_prop, "enum", mode_enum);
  json_object_set_new (data_properties, "mode", mode_prop);


  json_t *comm_prop = json_object ();


  json_object_set_new (comm_prop, "type", json_string ("string"));
  json_object_set_new (data_properties, "commodity", comm_prop);


  json_t *amount_prop = json_object ();


  json_object_set_new (amount_prop, "type", json_string ("integer"));
  json_object_set_new (amount_prop, "minimum", json_integer (1));
  json_object_set_new (data_properties, "amount", amount_prop);


  json_t *idem_prop = json_object ();


  json_object_set_new (idem_prop, "type", json_string ("string"));
  json_object_set_new (data_properties, "idempotency_key", idem_prop);


  json_t *data_required = json_array ();


  json_array_append_new (data_required, json_string ("sector_id"));
  json_array_append_new (data_required, json_string ("port_id"));
  json_array_append_new (data_required, json_string ("mode"));
  json_array_append_new (data_required, json_string ("idempotency_key"));


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/port.rob.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_properties);
  json_object_set_new (data_schema, "required", data_required);
  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));


  return data_schema;
}


/* --- Trade --- */
json_t *
schema_trade_port_info (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/trade.port_info.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_trade_sell (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/trade.sell.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("sector_id"));
  json_array_append_new (required, json_string ("port_id"));
  json_array_append_new (required, json_string ("items"));
  json_array_append_new (required, json_string ("idempotency_key"));
  json_array_append_new (required, json_string ("account"));
  json_object_set_new (root, "required", required);


  json_object_set_new (root, "additionalProperties", json_boolean (0));


  json_t *props = json_object ();


  json_object_set_new (root, "properties", props);


  json_t *sector_id_prop = json_object ();


  json_object_set_new (sector_id_prop, "type", json_string ("integer"));
  json_object_set_new (props, "sector_id", sector_id_prop);


  json_t *port_id_prop = json_object ();


  json_object_set_new (port_id_prop, "type", json_string ("integer"));
  json_object_set_new (props, "port_id", port_id_prop);


  json_t *idem_prop = json_object ();


  json_object_set_new (idem_prop, "type", json_string ("string"));
  json_object_set_new (props, "idempotency_key", idem_prop);


  json_t *account_prop = json_object ();


  json_object_set_new (account_prop, "type", json_string ("integer"));
  json_t *account_enum = json_array ();


  json_array_append_new (account_enum, json_integer (0));
  json_array_append_new (account_enum, json_integer (1));
  json_object_set_new (account_prop, "enum", account_enum);
  json_object_set_new (props, "account", account_prop);


  JSON_AUTO item_schema = json_object ();


  json_object_set_new (item_schema, "type", json_string ("object"));
  json_t *item_required = json_array ();


  json_array_append_new (item_required, json_string ("commodity"));
  json_array_append_new (item_required, json_string ("quantity"));
  json_object_set_new (item_schema, "required", item_required);
  json_object_set_new (item_schema, "additionalProperties", json_boolean (0));


  json_t *item_props = json_object ();


  json_object_set_new (item_schema, "properties", item_props);


  json_t *item_comm_prop = json_object ();


  json_object_set_new (item_comm_prop, "type", json_string ("string"));
  json_object_set_new (item_props, "commodity", item_comm_prop);


  json_t *item_qty_prop = json_object ();


  json_object_set_new (item_qty_prop, "type", json_string ("integer"));
  json_object_set_new (item_props, "quantity", item_qty_prop);


  json_t *items_prop = json_object ();


  json_object_set_new (items_prop, "type", json_string ("array"));
  json_object_set (items_prop, "items", item_schema);
  json_object_set_new (props, "items", items_prop);


  return root;
}


json_t *
schema_trade_quote (void)
{
  json_t *data_props = json_object ();


  json_t *port_id_prop = json_object ();
  json_object_set_new (port_id_prop, "type", json_string ("integer"));
  json_object_set_new (data_props, "port_id", port_id_prop);


  json_t *comm_prop = json_object ();


  json_object_set_new (comm_prop, "type", json_string ("string"));
  json_object_set_new (data_props, "commodity", comm_prop);


  json_t *qty_prop = json_object ();


  json_object_set_new (qty_prop, "type", json_string ("integer"));
  json_object_set_new (data_props, "quantity", qty_prop);


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/trade.quote.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_props);


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("port_id"));
  json_array_append_new (required, json_string ("commodity"));
  json_array_append_new (required, json_string ("quantity"));
  json_object_set_new (data_schema, "required", required);


  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));

  return data_schema;
}


json_t *
schema_trade_jettison (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/trade.jettison.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_trade_offer (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/trade.offer.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_trade_accept (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root,
                       "$id", json_string ("ge://schema/trade.accept.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_trade_cancel (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root,
                       "$id", json_string ("ge://schema/trade.cancel.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_trade_history (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/trade.history.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));
  json_object_set_new (root, "required", json_array ());
  json_object_set_new (root, "additionalProperties", json_boolean (0));


  json_t *props = json_object ();


  json_object_set_new (root, "properties", props);


  json_t *limit_prop = json_object ();


  json_object_set_new (limit_prop, "type", json_string ("integer"));
  json_object_set_new (limit_prop, "minimum", json_integer (1));
  json_object_set_new (limit_prop, "maximum", json_integer (50));
  json_object_set_new (props, "limit", limit_prop);


  json_t *cursor_prop = json_object ();


  json_object_set_new (cursor_prop, "type", json_string ("string"));
  json_object_set_new (props, "cursor", cursor_prop);


  return root;
}


json_t *
schema_port_update (void)
{
  // Reusing the structure of trade.port_info response for the event payload
  json_t *commodity_item_schema = json_object ();
  json_object_set_new (commodity_item_schema, "type", json_string ("object"));


  json_t *commodity_item_props = json_object ();


  json_t *code_prop = json_object ();


  json_object_set_new (code_prop, "type", json_string ("string"));
  json_object_set_new (commodity_item_props, "code", code_prop);


  json_t *qty_prop = json_object ();


  json_object_set_new (qty_prop, "type", json_string ("integer"));
  json_object_set_new (commodity_item_props, "quantity", qty_prop);


  json_t *buy_price_prop = json_object ();


  json_object_set_new (buy_price_prop, "type", json_string ("integer"));
  json_object_set_new (commodity_item_props, "buy_price", buy_price_prop);


  json_t *sell_price_prop = json_object ();


  json_object_set_new (sell_price_prop, "type", json_string ("integer"));
  json_object_set_new (commodity_item_props, "sell_price", sell_price_prop);


  json_t *capacity_prop = json_object ();


  json_object_set_new (capacity_prop, "type", json_string ("integer"));
  json_object_set_new (commodity_item_props, "capacity", capacity_prop);


  json_t *illegal_prop = json_object ();


  json_object_set_new (illegal_prop, "type", json_string ("boolean"));
  json_object_set_new (commodity_item_props, "illegal", illegal_prop);


  json_object_set_new (commodity_item_schema, "properties",
                       commodity_item_props);


  json_t *commodity_item_required = json_array ();


  json_array_append_new (commodity_item_required, json_string ("code"));
  json_array_append_new (commodity_item_required, json_string ("quantity"));
  json_array_append_new (commodity_item_required, json_string ("buy_price"));
  json_array_append_new (commodity_item_required, json_string ("sell_price"));
  json_array_append_new (commodity_item_required, json_string ("capacity"));
  json_array_append_new (commodity_item_required, json_string ("illegal"));
  json_object_set_new (commodity_item_schema,
                       "required", commodity_item_required);


  json_object_set_new (commodity_item_schema,
                       "additionalProperties", json_boolean (0));


  json_t *port_properties = json_object ();


  json_t *id_prop = json_object ();


  json_object_set_new (id_prop, "type", json_string ("integer"));
  json_object_set_new (port_properties, "id", id_prop);


  json_t *number_prop = json_object ();


  json_object_set_new (number_prop, "type", json_string ("integer"));
  json_object_set_new (port_properties, "number", number_prop);


  json_t *name_prop = json_object ();


  json_object_set_new (name_prop, "type", json_string ("string"));
  json_object_set_new (port_properties, "name", name_prop);


  json_t *sector_prop = json_object ();


  json_object_set_new (sector_prop, "type", json_string ("integer"));
  json_object_set_new (port_properties, "sector", sector_prop);


  json_t *size_prop = json_object ();


  json_object_set_new (size_prop, "type", json_string ("integer"));
  json_object_set_new (port_properties, "size", size_prop);


  json_t *techlevel_prop = json_object ();


  json_object_set_new (techlevel_prop, "type", json_string ("integer"));
  json_object_set_new (port_properties, "techlevel", techlevel_prop);


  json_t *petty_cash_prop = json_object ();


  json_object_set_new (petty_cash_prop, "type", json_string ("integer"));
  json_object_set_new (port_properties, "petty_cash", petty_cash_prop);


  json_t *type_prop = json_object ();


  json_object_set_new (type_prop, "type", json_string ("integer"));
  json_object_set_new (port_properties, "type", type_prop);


  json_t *commodities_prop = json_object ();


  json_object_set_new (commodities_prop, "type", json_string ("array"));
  json_object_set (commodities_prop, "items", commodity_item_schema);
  json_object_set_new (port_properties, "commodities", commodities_prop);


  json_t *port_required = json_array ();


  json_array_append_new (port_required, json_string ("id"));
  json_array_append_new (port_required, json_string ("number"));
  json_array_append_new (port_required, json_string ("name"));
  json_array_append_new (port_required, json_string ("sector"));
  json_array_append_new (port_required, json_string ("size"));
  json_array_append_new (port_required, json_string ("techlevel"));
  json_array_append_new (port_required, json_string ("petty_cash"));
  json_array_append_new (port_required, json_string ("type"));
  json_array_append_new (port_required, json_string ("commodities"));


  json_t *data_properties = json_object ();
  json_t *port_obj = json_object ();


  json_object_set_new (port_obj, "type", json_string ("object"));
  json_object_set_new (port_obj, "properties", port_properties);
  json_object_set_new (port_obj, "required", port_required);
  json_object_set_new (data_properties, "port", port_obj);


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/port.update.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_properties);


  json_t *root_required = json_array ();


  json_array_append_new (root_required, json_string ("port"));
  json_object_set_new (data_schema, "required", root_required);


  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));


  json_decref (commodity_item_schema);
  return data_schema;
}


/* --- Move --- */
json_t *
schema_move_describe_sector (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/move.describe_sector.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_move_scan (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/move.scan.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));
  json_object_set_new (root, "properties", json_object ());
  return root;
}


json_t *
schema_move_pathfind (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/move.pathfind.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_move_autopilot_start (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/move.autopilot.start.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_move_autopilot_stop (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/move.autopilot.stop.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_move_autopilot_status (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string
                         ("ge://schema/move.autopilot.status.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));
  json_object_set_new (root, "properties", json_object ());
  // Empty properties object
  json_object_set_new (root, "required", json_array ());
  // No required properties
  json_object_set_new (root, "additionalProperties", json_boolean (0));
  return root;
}


/* --- Sector --- */
json_t *
schema_sector_info (void)
{
  json_t *data_properties = json_object ();
  json_t *sector_id_prop = json_object ();
  json_object_set_new (sector_id_prop, "type", json_string ("integer"));
  json_object_set_new (data_properties, "sector_id", sector_id_prop);


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/sector.info.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_properties);
  json_object_set_new (data_schema, "required", json_array ());
  // No required properties
  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));
  return data_schema;
}


json_t *
schema_sector_search (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/sector.search.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_sector_scan_density (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/sector.scan.density.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_sector_scan (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/sector.scan.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


/* --- Planet --- */
json_t *
schema_planet_genesis (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/planet.genesis.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_planet_info (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/planet.info.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_planet_rename (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/planet.rename.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_planet_land (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/planet.land.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_planet_launch (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/planet.launch.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_planet_transfer_ownership (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root,
                       "$id",
                       json_string
                         ("ge://schema/planet.transfer_ownership.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_planet_harvest (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/planet.harvest.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_planet_deposit (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/planet.deposit.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_planet_withdraw (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/planet.withdraw.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_planet_genesis_create (void)
{
  json_t *data_properties = json_object ();


  json_t *sector_id_prop = json_object ();
  json_object_set_new (sector_id_prop, "type", json_string ("integer"));
  json_object_set_new (data_properties, "sector_id", sector_id_prop);


  json_t *name_prop = json_object ();


  json_object_set_new (name_prop, "type", json_string ("string"));
  json_object_set_new (data_properties, "name", name_prop);


  json_t *owner_prop = json_object ();


  json_object_set_new (owner_prop, "type", json_string ("string"));
  json_t *owner_enum = json_array ();


  json_array_append_new (owner_enum, json_string ("player"));
  json_array_append_new (owner_enum, json_string ("corporation"));
  json_object_set_new (owner_prop, "enum", owner_enum);
  json_object_set_new (data_properties, "owner_entity_type", owner_prop);


  json_t *idem_prop = json_object ();


  json_object_set_new (idem_prop, "type", json_string ("string"));
  json_object_set_new (data_properties, "idempotency_key", idem_prop);


  json_t *data_required = json_array ();


  json_array_append_new (data_required, json_string ("sector_id"));
  json_array_append_new (data_required, json_string ("name"));
  json_array_append_new (data_required, json_string ("owner_entity_type"));


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string
                         ("ge://schema/planet.genesis_create.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_properties);
  json_object_set_new (data_schema, "required", data_required);
  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));
  return data_schema;
}


/* --- Citadel --- */
json_t *
schema_citadel_build (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/citadel.build.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_citadel_upgrade (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/citadel.upgrade.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


/* --- Combat --- */
json_t *
schema_combat_attack (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/combat.attack.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_combat_deploy_fighters (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string
                         ("ge://schema/combat.deploy_fighters.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("amount"));
  json_array_append_new (required, json_string ("offense"));
  json_object_set_new (root, "required", required);


  json_object_set_new (root, "additionalProperties", json_boolean (0));


  json_t *props = json_object ();


  json_object_set_new (root, "properties", props);


  json_t *amount_prop = json_object ();


  json_object_set_new (amount_prop, "type", json_string ("integer"));
  json_object_set_new (props, "amount", amount_prop);


  json_t *offense_prop = json_object ();


  json_object_set_new (offense_prop, "type", json_string ("integer"));
  json_object_set_new (props, "offense", offense_prop);


  json_t *corp_id_prop = json_object ();


  json_object_set_new (corp_id_prop, "type", json_string ("integer"));
  json_object_set_new (props, "corporation_id", corp_id_prop);


  return root;
}


json_t *
schema_combat_lay_mines (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/combat.lay_mines.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_combat_sweep_mines (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/combat.sweep_mines.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_combat_status (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/combat.status.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_fighters_recall (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/fighters.recall.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("sector_id"));
  json_array_append_new (required, json_string ("asset_id"));
  json_object_set_new (root, "required", required);


  json_object_set_new (root, "additionalProperties", json_boolean (0));


  json_t *props = json_object ();


  json_object_set_new (root, "properties", props);


  json_t *sector_id_prop = json_object ();


  json_object_set_new (sector_id_prop, "type", json_string ("integer"));
  json_object_set_new (props, "sector_id", sector_id_prop);


  json_t *asset_id_prop = json_object ();


  json_object_set_new (asset_id_prop, "type", json_string ("integer"));
  json_object_set_new (props, "asset_id", asset_id_prop);


  return root;
}


json_t *
schema_combat_deploy_mines (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/combat.deploy_mines.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("amount"));
  json_array_append_new (required, json_string ("offense"));
  json_object_set_new (root, "required", required);


  json_object_set_new (root, "additionalProperties", json_boolean (0));


  json_t *props = json_object ();


  json_object_set_new (root, "properties", props);


  json_t *amount_prop = json_object ();


  json_object_set_new (amount_prop, "type", json_string ("integer"));
  json_object_set_new (props, "amount", amount_prop);


  json_t *offense_prop = json_object ();


  json_object_set_new (offense_prop, "type", json_string ("integer"));
  json_object_set_new (props, "offense", offense_prop);


  json_t *corp_prop = json_object ();


  json_object_set_new (corp_prop, "type", json_string ("integer"));
  json_object_set_new (props, "corporation_id", corp_prop);


  json_t *mine_type_prop = json_object ();


  json_object_set_new (mine_type_prop, "type", json_string ("integer"));
  json_object_set_new (props, "mine_type", mine_type_prop);


  return root;
}


json_t *
schema_mines_recall (void)
{
  json_t *root = json_object ();
  json_object_set_new (root,
                       "$id", json_string ("ge://schema/mines.recall.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("sector_id"));
  json_array_append_new (required, json_string ("asset_id"));
  json_object_set_new (root, "required", required);


  json_object_set_new (root, "additionalProperties", json_boolean (0));


  json_t *props = json_object ();


  json_object_set_new (root, "properties", props);


  json_t *sector_id_prop = json_object ();


  json_object_set_new (sector_id_prop, "type", json_string ("integer"));
  json_object_set_new (props, "sector_id", sector_id_prop);


  json_t *asset_id_prop = json_object ();


  json_object_set_new (asset_id_prop, "type", json_string ("integer"));
  json_object_set_new (props, "asset_id", asset_id_prop);


  return root;
}


/* --- Deploy --- */
json_t *
schema_deploy_fighters_list (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/deploy.fighters.list.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));
  json_object_set_new (root, "properties", json_object ());
  // Empty properties object
  json_object_set_new (root, "required", json_array ());
  // No required properties
  json_object_set_new (root, "additionalProperties", json_boolean (0));
  return root;
}


json_t *
schema_deploy_mines_list (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/deploy.mines.list.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));
  json_object_set_new (root, "properties", json_object ());
  // Empty properties object
  json_object_set_new (root, "required", json_array ());
  // No required properties
  json_object_set_new (root, "additionalProperties", json_boolean (0));
  return root;
}


/* --- Chat --- */
json_t *
schema_chat_send (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/chat.send.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_chat_broadcast (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/chat.broadcast.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_chat_history (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root,
                       "$id", json_string ("ge://schema/chat.history.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


/* --- Mail --- */
json_t *
schema_mail_send (void)
{
  json_t *data_properties = json_object ();


  json_t *to_prop = json_object ();
  json_object_set_new (to_prop, "type", json_string ("string"));
  json_object_set_new (data_properties, "to_player_name", to_prop);


  json_t *subject_prop = json_object ();


  json_object_set_new (subject_prop, "type", json_string ("string"));
  json_object_set_new (data_properties, "subject", subject_prop);


  json_t *body_prop = json_object ();


  json_object_set_new (body_prop, "type", json_string ("string"));
  json_object_set_new (data_properties, "body", body_prop);


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/mail.send.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_properties);


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("to_player_name"));
  json_array_append_new (required, json_string ("subject"));
  json_array_append_new (required, json_string ("body"));
  json_object_set_new (data_schema, "required", required);


  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));


  return data_schema;
}


json_t *
schema_mail_inbox (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/mail.inbox.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));
  json_object_set_new (root, "properties", json_object ());
  // Empty properties object
  json_object_set_new (root, "required", json_array ());
  // No required properties
  json_object_set_new (root, "additionalProperties", json_boolean (0));
  return root;
}


json_t *
schema_mail_read (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/mail.read.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("mail_id"));
  json_object_set_new (root, "required", required);


  json_object_set_new (root, "additionalProperties", json_boolean (0));


  json_t *props = json_object ();


  json_object_set_new (root, "properties", props);


  json_t *mail_id_prop = json_object ();


  json_object_set_new (mail_id_prop, "type", json_string ("integer"));
  json_object_set_new (props, "mail_id", mail_id_prop);


  return root;
}


json_t *
schema_mail_delete (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/mail.delete.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


/* --- Notice --- */
json_t *
schema_sys_notice_create (void)
{
  json_t *data_properties = json_object ();


  json_t *title_prop = json_object ();
  json_object_set_new (title_prop, "type", json_string ("string"));
  json_object_set_new (data_properties, "title", title_prop);


  json_t *body_prop = json_object ();


  json_object_set_new (body_prop, "type", json_string ("string"));
  json_object_set_new (data_properties, "body", body_prop);


  json_t *severity_prop = json_object ();


  json_object_set_new (severity_prop, "type", json_string ("string"));
  json_object_set_new (data_properties, "severity", severity_prop);


  json_t *expires_at_prop = json_object ();


  json_object_set_new (expires_at_prop, "type", json_string ("integer"));
  json_object_set_new (data_properties, "expires_at", expires_at_prop);


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/sys.notice.create.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_properties);


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("title"));
  json_array_append_new (required, json_string ("body"));
  json_object_set_new (data_schema, "required", required);


  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));


  return data_schema;
}


json_t *
schema_notice_list (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/notice.list.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));
  json_object_set_new (root, "properties", json_object ());
  // Empty properties object
  json_object_set_new (root, "required", json_array ());
  // No required properties
  json_object_set_new (root, "additionalProperties", json_boolean (0));
  return root;
}


json_t *
schema_notice_ack (void)
{
  json_t *data_properties = json_object ();
  json_t *notice_id_prop = json_object ();
  json_object_set_new (notice_id_prop, "type", json_string ("integer"));
  json_object_set_new (data_properties, "notice_id", notice_id_prop);


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/notice.ack.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_properties);


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("notice_id"));
  json_object_set_new (data_schema, "required", required);


  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));
  return data_schema;
}


/* --- Insurance --- */
json_t *
schema_insurance_policies_list (void)
{
  json_t *data_schema = json_object ();
  json_object_set_new (data_schema, "$id",
                       json_string
                         ("ge://schema/insurance.policies.list.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", json_object ());
  json_object_set_new (data_schema, "required", json_array ());
  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));
  return data_schema;
}


json_t *
schema_insurance_policies_buy (void)
{
  json_t *data_properties = json_object ();


  json_t *subject_type_prop = json_object ();
  json_object_set_new (subject_type_prop, "type", json_string ("string"));
  json_t *subject_type_enum = json_array ();


  json_array_append_new (subject_type_enum, json_string ("ship"));
  json_array_append_new (subject_type_enum, json_string ("cargo"));
  json_array_append_new (subject_type_enum, json_string ("planet"));
  json_object_set_new (subject_type_prop, "enum", subject_type_enum);
  json_object_set_new (data_properties, "subject_type", subject_type_prop);


  json_t *subject_id_prop = json_object ();


  json_object_set_new (subject_id_prop, "type", json_string ("integer"));
  json_object_set_new (data_properties, "subject_id", subject_id_prop);


  json_t *duration_prop = json_object ();


  json_object_set_new (duration_prop, "type", json_string ("integer"));
  json_object_set_new (data_properties, "duration", duration_prop);


  json_t *premium_prop = json_object ();


  json_object_set_new (premium_prop, "type", json_string ("integer"));
  json_object_set_new (data_properties, "premium", premium_prop);


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string
                         ("ge://schema/insurance.policies.buy.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_properties);


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("subject_type"));
  json_array_append_new (required, json_string ("subject_id"));
  json_array_append_new (required, json_string ("duration"));
  json_array_append_new (required, json_string ("premium"));
  json_object_set_new (data_schema, "required", required);


  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));
  return data_schema;
}


json_t *
schema_insurance_claim_file (void)
{
  json_t *data_properties = json_object ();


  json_t *policy_id_prop = json_object ();
  json_object_set_new (policy_id_prop, "type", json_string ("integer"));
  json_object_set_new (data_properties, "policy_id", policy_id_prop);


  json_t *incident_desc_prop = json_object ();


  json_object_set_new (incident_desc_prop, "type", json_string ("string"));
  json_object_set_new (data_properties,
                       "incident_description", incident_desc_prop);


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/insurance.claim.file.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_properties);


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("policy_id"));
  json_array_append_new (required, json_string ("incident_description"));
  json_object_set_new (data_schema, "required", required);


  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));
  return data_schema;
}


/* --- Fines --- */
json_t *
schema_fine_list (void)
{
  json_t *data_schema = json_object ();
  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/fine.list.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", json_object ());
  json_object_set_new (data_schema, "required", json_array ());
  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));
  return data_schema;
}


json_t *
schema_fine_pay (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/fine.pay.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));


  json_t *props = json_object ();


  json_object_set_new (root, "properties", props);


  json_t *fine_id_prop = json_object ();


  json_object_set_new (fine_id_prop, "type", json_string ("integer"));
  json_object_set_new (props, "fine_id", fine_id_prop);


  json_t *amount_prop = json_object ();


  json_object_set_new (amount_prop, "type", json_string ("integer"));
  json_object_set_new (props, "amount", amount_prop);


  json_t *req = json_array ();


  json_array_append_new (req, json_string ("fine_id"));


  json_object_set_new (root, "required", req);


  json_object_set_new (root, "additionalProperties", json_false ());


  return root;
}


/* --- News --- */
json_t *
schema_news_read (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/news.read.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));
  json_object_set_new (root, "properties", json_object ());
  // Empty properties object
  json_object_set_new (root, "required", json_array ());
  // No required properties
  json_object_set_new (root, "additionalProperties", json_boolean (0));
  return root;
}


/* --- Subscribe --- */
json_t *
schema_subscribe_add (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/subscribe.add.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_subscribe_remove (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/subscribe.remove.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_subscribe_list (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/subscribe.list.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


json_t *
schema_subscribe_catalog (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/subscribe.catalog.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));
  json_object_set_new (root, "properties", json_object ());
  // Empty properties object
  json_object_set_new (root, "required", json_array ());
  // No required properties
  json_object_set_new (root, "additionalProperties", json_boolean (0));
  return root;
}


/* --- Bulk --- */
json_t *
schema_bulk_execute (void)
{
  /* TODO: Implement this schema */
  json_t *root = json_object ();
  json_object_set_new (root,
                       "$id", json_string ("ge://schema/bulk.execute.json"));
  json_object_set_new (root, "$comment",
                       json_string ("Schema not yet implemented"));
  return root;
}


/* --- Player --- */
json_t *
schema_player_set_trade_account_preference (void)
{
  json_t *data_props = json_object ();


  json_t *pref_prop = json_object ();
  json_object_set_new (pref_prop, "type", json_string ("integer"));
  json_t *pref_enum = json_array ();


  json_array_append_new (pref_enum, json_integer (0));
  json_array_append_new (pref_enum, json_integer (1));
  json_object_set_new (pref_prop, "enum", pref_enum);
  json_object_set_new (data_props, "preference", pref_prop);


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema,
                       "$id",
                       json_string
                         ("ge://schema/player.set_trade_account_preference.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_props);


  json_t *required = json_array ();


  json_array_append_new (required, json_string ("preference"));
  json_object_set_new (data_schema, "required", required);


  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));
  return data_schema;
}


/* --- Player --- */
json_t *
schema_player_my_info (void)
{
  json_t *data_schema = json_object ();
  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/player.my_info.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", json_object ());
  // Empty properties object
  json_object_set_new (data_schema, "required", json_array ());
  // No required properties
  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));
  return data_schema;
}


/* --- Player --- */
json_t *
schema_player_info (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/player.info.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));

  json_t *props = json_object ();


  json_object_set_new (root, "properties", props);

  /* Player Object */
  json_t *player = json_object ();


  json_object_set_new (props, "player", player);
  json_object_set_new (player, "type", json_string ("object"));
  json_t *p_props = json_object ();


  json_object_set_new (player, "properties", p_props);

  json_t *id_prop = json_object ();


  json_object_set_new (id_prop, "type", json_string ("integer"));
  json_object_set_new (p_props, "id", id_prop);


  json_t *user_prop = json_object ();


  json_object_set_new (user_prop, "type", json_string ("string"));
  json_object_set_new (p_props, "username", user_prop);


  json_t *credits_prop = json_object ();


  json_object_set_new (credits_prop, "type", json_string ("string"));
  json_object_set_new (p_props, "credits", credits_prop);


  json_t *exp_prop = json_object ();


  json_object_set_new (exp_prop, "type", json_string ("integer"));
  json_object_set_new (p_props, "experience", exp_prop);


  json_t *align_prop = json_object ();


  json_object_set_new (align_prop, "type", json_string ("integer"));
  json_object_set_new (p_props, "alignment", align_prop);


  json_t *score_prop = json_object ();


  json_object_set_new (score_prop, "type", json_string ("integer"));
  json_object_set_new (p_props, "score", score_prop);

  /* Ships Array */
  json_t *ships = json_object ();


  json_object_set_new (props, "ships", ships);
  json_object_set_new (ships, "type", json_string ("array"));
  json_t *ship_item = json_object ();


  json_object_set_new (ships, "items", ship_item);
  json_object_set_new (ship_item, "type", json_string ("object"));

  json_t *ship_props = json_object ();


  json_object_set_new (ship_item, "properties", ship_props);
  json_t *ship_id_prop = json_object ();


  json_object_set_new (ship_id_prop, "type", json_string ("integer"));
  json_object_set_new (ship_props, "id", ship_id_prop);


  json_t *ship_name_prop = json_object ();


  json_object_set_new (ship_name_prop, "type", json_string ("string"));
  json_object_set_new (ship_props, "name", ship_name_prop);

  json_object_set_new (root, "additionalProperties", 0);
  return root;
}


json_t *
schema_player_list_online_request (void)
{
  json_t *properties = json_object ();
  json_t *required = json_array ();     // No required fields for flexibility


  // offset property
  json_t *offset_prop = json_object ();
  json_object_set_new (offset_prop, "type", json_string ("integer"));
  json_object_set_new (offset_prop, "default", json_integer (0));
  json_object_set_new (properties, "offset", offset_prop);


  // limit property
  json_t *limit_prop = json_object ();


  json_object_set_new (limit_prop, "type", json_string ("integer"));
  json_object_set_new (limit_prop, "minimum", json_integer (1));
  json_object_set_new (limit_prop, "maximum", json_integer (1000));
  json_object_set_new (properties, "limit", limit_prop);


  // fields property (array of strings)
  json_t *fields_items_schema = json_object ();


  json_object_set_new (fields_items_schema, "type", json_string ("string"));


  json_t *fields_schema = json_object ();


  json_object_set_new (fields_schema, "type", json_string ("array"));
  json_object_set (fields_schema, "items", fields_items_schema);
  json_object_set_new (fields_schema, "uniqueItems", json_true ());
  // Note: fields_items_schema refcount is 1 (owned by fields_schema items), so we need to decref our local ref if we used json_object_set (incref)
  // But wait, json_object_set (not _new) increfs.
  // Above: json_object_set (fields_schema, "items", fields_items_schema); -> increfs fields_items_schema.
  // So fields_items_schema refcount is 2 (1 local + 1 array).
  // We should decref local ref.
  json_decref (fields_items_schema);


  json_object_set_new (properties, "fields", fields_schema);


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string
                         ("ge://schema/player.list_online.request.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", properties);
  json_object_set_new (data_schema, "required", required);
  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));


  return data_schema;
}


json_t *
schema_player_list_online_response (void)
{
  json_t *properties = json_object ();
  json_t *required = json_array ();


  // total_online property
  json_t *total_online_prop = json_object ();
  json_object_set_new (total_online_prop, "type", json_string ("integer"));
  json_object_set_new (properties, "total_online", total_online_prop);
  json_array_append_new (required, json_string ("total_online"));


  // returned_count property
  json_t *returned_count_prop = json_object ();


  json_object_set_new (returned_count_prop, "type", json_string ("integer"));
  json_object_set_new (properties, "returned_count", returned_count_prop);
  json_array_append_new (required, json_string ("returned_count"));


  // offset property
  json_t *offset_prop = json_object ();


  json_object_set_new (offset_prop, "type", json_string ("integer"));
  json_object_set_new (properties, "offset", offset_prop);
  json_array_append_new (required, json_string ("offset"));


  // limit property
  json_t *limit_prop = json_object ();


  json_object_set_new (limit_prop, "type", json_string ("integer"));
  json_object_set_new (properties, "limit", limit_prop);
  json_array_append_new (required, json_string ("limit"));


  // players property (array of player objects)
  json_t *player_item_schema = schema_player_info ();   // Reuse existing player info schema


  json_t *players_prop = json_object ();


  json_object_set_new (players_prop, "type", json_string ("array"));
  json_object_set_new (players_prop, "items", player_item_schema);
  json_object_set_new (properties, "players", players_prop);
  json_array_append_new (required, json_string ("players"));


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string
                         ("ge://schema/player.list_online.response.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", properties);
  json_object_set_new (data_schema, "required", required);
  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));


  return data_schema;
}


json_t *
schema_bank_balance (void)
{
  json_t *data_schema = json_object ();
  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/bank.balance.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", json_object ());
  json_object_set_new (data_schema, "required", json_array ());
  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));
  return data_schema;
}


json_t *
schema_bank_history (void)
{
  json_t *root = json_object ();
  json_object_set_new (root,
                       "$id", json_string ("ge://schema/bank.history.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));
  json_object_set_new (root, "required", json_array ());
  json_object_set_new (root, "additionalProperties", json_boolean (0));


  json_t *props = json_object ();


  json_object_set_new (root, "properties", props);


  json_t *limit_prop = json_object ();


  json_object_set_new (limit_prop, "type", json_string ("integer"));
  json_object_set_new (limit_prop, "minimum", json_integer (1));
  json_object_set_new (limit_prop, "maximum", json_integer (50));
  json_object_set_new (props, "limit", limit_prop);


  json_t *cursor_prop = json_object ();


  json_object_set_new (cursor_prop, "type", json_string ("string"));
  json_object_set_new (props, "cursor", cursor_prop);


  json_t *tx_type_enum = json_array ();


  json_array_append_new (tx_type_enum, json_string ("DEPOSIT"));
  json_array_append_new (tx_type_enum, json_string ("WITHDRAWAL"));
  json_array_append_new (tx_type_enum, json_string ("TRANSFER"));
  json_array_append_new (tx_type_enum, json_string ("INTEREST"));
  json_array_append_new (tx_type_enum, json_string ("FEE"));
  json_array_append_new (tx_type_enum, json_string ("WIRE"));
  json_array_append_new (tx_type_enum, json_string ("TAX"));
  json_array_append_new (tx_type_enum, json_string ("TRADE_BUY_FEE"));
  json_array_append_new (tx_type_enum, json_string ("TRADE_SELL_FEE"));
  json_array_append_new (tx_type_enum, json_string ("WITHDRAWAL_FEE"));
  json_array_append_new (tx_type_enum, json_string ("ADJUSTMENT"));


  json_t *tx_type_prop = json_object ();


  json_object_set_new (tx_type_prop, "type", json_string ("string"));
  json_object_set_new (tx_type_prop, "enum", tx_type_enum);
  json_object_set_new (props, "tx_type", tx_type_prop);


  json_t *start_date_prop = json_object ();


  json_object_set_new (start_date_prop, "type", json_string ("integer"));
  json_object_set_new (props, "start_date", start_date_prop);


  json_t *end_date_prop = json_object ();


  json_object_set_new (end_date_prop, "type", json_string ("integer"));
  json_object_set_new (props, "end_date", end_date_prop);


  json_t *min_amount_prop = json_object ();


  json_object_set_new (min_amount_prop, "type", json_string ("integer"));
  json_object_set_new (min_amount_prop, "minimum", json_integer (0));
  json_object_set_new (props, "min_amount", min_amount_prop);


  json_t *max_amount_prop = json_object ();


  json_object_set_new (max_amount_prop, "type", json_string ("integer"));
  json_object_set_new (max_amount_prop, "minimum", json_integer (0));
  json_object_set_new (props, "max_amount", max_amount_prop);


  return root;
}


json_t *
schema_bank_leaderboard (void)
{
  json_t *data_properties = json_object ();


  json_t *limit_prop = json_object ();
  json_object_set_new (limit_prop, "type", json_string ("integer"));
  json_object_set_new (limit_prop, "minimum", json_integer (1));
  json_object_set_new (limit_prop, "maximum", json_integer (100));
  json_object_set_new (data_properties, "limit", limit_prop);


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/bank.leaderboard.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_properties);
  json_object_set_new (data_schema, "required", json_array ());
  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));
  return data_schema;
}


json_t *
schema_hardware_list (void)
{
  json_t *root = json_object ();
  json_object_set_new (root, "$id",
                       json_string ("ge://schema/hardware.list.json"));
  json_object_set_new (root, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (root, "type", json_string ("object"));
  json_object_set_new (root, "properties", json_object ());
  json_object_set_new (root, "required", json_array ());
  json_object_set_new (root, "additionalProperties", json_boolean (0));
  return root;
}


json_t *
schema_hardware_buy (void)
{
  json_t *data_properties = json_object ();


  json_t *code_prop = json_object ();
  json_object_set_new (code_prop, "type", json_string ("string"));
  json_object_set_new (data_properties, "code", code_prop);


  json_t *qty_prop = json_object ();


  json_object_set_new (qty_prop, "type", json_string ("integer"));
  json_object_set_new (qty_prop, "minimum", json_integer (1));
  json_object_set_new (data_properties, "quantity", qty_prop);


  json_t *idem_prop = json_object ();


  json_object_set_new (idem_prop, "type", json_string ("string"));
  json_object_set_new (data_properties, "idempotency_key", idem_prop);


  json_t *data_required = json_array ();


  json_array_append_new (data_required, json_string ("code"));
  json_array_append_new (data_required, json_string ("quantity"));


  json_t *data_schema = json_object ();


  json_object_set_new (data_schema, "$id",
                       json_string ("ge://schema/hardware.buy.json"));
  json_object_set_new (data_schema, "$schema",
                       json_string
                         ("https://json-schema.org/draft/2020-12/schema"));
  json_object_set_new (data_schema, "type", json_string ("object"));
  json_object_set_new (data_schema, "properties", data_properties);
  json_object_set_new (data_schema, "required", data_required);
  json_object_set_new (data_schema, "additionalProperties", json_boolean (0));
  return data_schema;
}

