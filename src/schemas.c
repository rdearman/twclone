#include <string.h>
#include <jansson.h>
#include "schemas.h"
#include <stdlib.h>


static char *
why_dup (const char *m)
{
  size_t n = strlen (m) + 1;
  char *p = (char *) malloc (n);
  if (p)
    memcpy (p, m, n);
  return p;
}

int
schema_validate_payload (const char *type, json_t *payload, char **why)
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
      // Optional: filters or page_size ints later
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




/* ----- Capabilities ----- */
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

/* ----- Minimal JSON Schemas (illustrative) ----- */

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
schema_move_warp (void)
{
  return json_pack ("{s:s, s:s, s:s, s:{s:o}, s:o}",
		    "$id", "ge://schema/move.warp.json",
		    "$schema", "https://json-schema.org/draft/2020-12/schema",
		    "type", "object",
		    "properties", "data", json_pack ("{s:{s:s}}",
						     "properties",
						     "to_sector_id",
						     "integer"), "required",
		    json_pack ("[s,s]", "command", "data"));
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

/* Registry */
json_t *
schema_get (const char *key)
{
  if (!key)
    return NULL;
  if (strcmp (key, "envelope") == 0)
    return schema_envelope ();
  if (strcmp (key, "auth.login") == 0)
    return schema_auth_login ();
  if (strcmp (key, "move.warp") == 0)
    return schema_move_warp ();
  if (strcmp (key, "trade.buy") == 0)
    return schema_trade_buy ();
  /* Add more as you implement them */
  return NULL;
}

json_t *
schema_keys (void)
{
  return json_pack ("[s,s,s,s]",
		    "envelope", "auth.login", "move.warp", "trade.buy");
}
