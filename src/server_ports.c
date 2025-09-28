#include <jansson.h>
#include <string.h>
#include "server_ports.h"
#include "database.h"		// db_* for ports/trade
#include "errors.h"
#include "config.h"
#include "server_envelope.h"

#define RULE_REFUSE(_code,_msg,_hint_json) \
    do { send_enveloped_refused(ctx->fd, root, (_code), (_msg), (_hint_json)); goto trade_buy_done; } while (0)

#define RULE_ERROR(_code,_msg) \
    do { send_enveloped_error(ctx->fd, root, (_code), (_msg)); goto trade_buy_done; } while (0)

void idemp_fingerprint_json (json_t * obj, char out[17]);
void iso8601_utc (char out[32]);


int
cmd_trade_sell (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "trade.sell");
}

int
cmd_trade_offer (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "trade.offer");
}

int
cmd_trade_accept (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "trade.accept");
}

int
cmd_trade_cancel (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "trade.cancel");
}

int
cmd_trade_history (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "trade.history");
}



/* Build a minimal, stable JSON for fingerprinting (cmd + data subset) */
static json_t *
build_trade_buy_fp_obj (const char *cmd, json_t *jdata)
{
  /* Expect: port_id (int), commodity (str), quantity (int).
     Ignore meta and unrelated keys. */
  json_t *fp = json_object ();
  json_object_set_new (fp, "command", json_string (cmd));

  int port_id = 0, qty = 0;
  const char *commodity = NULL;
  json_t *jport = json_object_get (jdata, "port_id");
  json_t *jcomm = json_object_get (jdata, "commodity");
  json_t *jqty = json_object_get (jdata, "quantity");
  if (json_is_integer (jport))
    port_id = (int) json_integer_value (jport);
  if (json_is_integer (jqty))
    qty = (int) json_integer_value (jqty);
  if (json_is_string (jcomm))
    commodity = json_string_value (jcomm);

  json_object_set_new (fp, "port_id", json_integer (port_id));
  json_object_set_new (fp, "quantity", json_integer (qty));
  json_object_set_new (fp, "commodity",
		       json_string (commodity ? commodity : ""));
  return fp;			/* caller must json_decref */
}



/* ========= Trading ========= */

int
cmd_trade_buy (client_ctx_t *ctx, json_t *root)
{
  json_t *jdata = json_object_get (root, "data");
  if (!json_is_object (jdata))
    {
      send_enveloped_error (ctx->fd, root, 1301, "Missing required field");
    }
  else
    {
      /* Extract fields */
      json_t *jport = json_object_get (jdata, "port_id");
      json_t *jcomm = json_object_get (jdata, "commodity");
      json_t *jqty = json_object_get (jdata, "quantity");
      const char *commodity =
	json_is_string (jcomm) ? json_string_value (jcomm) : NULL;
      int port_id =
	json_is_integer (jport) ? (int) json_integer_value (jport) : 0;
      int qty = json_is_integer (jqty) ? (int) json_integer_value (jqty) : 0;

      if (!commodity || port_id <= 0 || qty <= 0)
	{
	  RULE_ERROR (ERR_BAD_REQUEST, "Missing required field");
	  //              send_enveloped_error (ctx->fd, root, 1301,
	  //                            "Missing required field");
	  /* no idempotency processing if invalid */
	}
      else
	{
	  /* Pull idempotency key if present */
	  const char *idem_key = NULL;
	  json_t *jmeta = json_object_get (root, "meta");
	  if (json_is_object (jmeta))
	    {
	      json_t *jk = json_object_get (jmeta, "idempotency_key");
	      if (json_is_string (jk))
		idem_key = json_string_value (jk);
	    }

	  /* Build fingerprint */
	  char fp[17];
	  fp[0] = 0;
	  int c;
	  json_t *fpobj = build_trade_buy_fp_obj (c, jdata);
	  idemp_fingerprint_json (fpobj, fp);
	  json_decref (fpobj);

	  if (idem_key && *idem_key)
	    {
	      /* Try to begin idempotent op */
	      int rc = db_idemp_try_begin (idem_key, c, fp);
	      if (rc == SQLITE_CONSTRAINT)
		{
		  /* Existing key: fetch */
		  char *ecmd = NULL, *efp = NULL, *erst = NULL;
		  if (db_idemp_fetch (idem_key, &ecmd, &efp, &erst) ==
		      SQLITE_OK)
		    {
		      int fp_match = (efp && strcmp (efp, fp) == 0);
		      if (!fp_match)
			{
			  /* Key reused with different payload */
			  send_enveloped_error (ctx->fd, root, 1105,
						"Duplicate request (idempotency key reused)");
			}
		      else if (erst)
			{
			  /* Replay stored response exactly */
			  json_error_t jerr;
			  json_t *env = json_loads (erst, 0, &jerr);
			  if (env)
			    {
			      send_all_json (ctx->fd, env);
			      json_decref (env);
			    }
			  else
			    {
			      /* Corrupt stored response; treat as server error */
			      send_enveloped_error (ctx->fd, root, 1500,
						    "Idempotency replay error");
			    }
			}
		      else
			{
			  /* Record exists but no stored response (in-flight/crash before store).
			     For now, treat as duplicate; later you could block/wait or retry op safely. */
			  send_enveloped_error (ctx->fd, root, 1105,
						"Duplicate request (pending)");
			}
		      free (ecmd);
		      free (efp);
		      free (erst);
		      /* Done */
		      goto done_trade_buy;
		    }
		  else
		    {
		      /* Couldn’t fetch; treat as server error */
		      send_enveloped_error (ctx->fd, root, 1500,
					    "Database error");
		      goto done_trade_buy;
		    }
		}
	      else if (rc != SQLITE_OK)
		{
		  send_enveloped_error (ctx->fd, root, 1500,
					"Database error");
		  goto done_trade_buy;
		}
	      /* If SQLITE_OK, we “own” this key now and should execute then store. */
	    }

	  /* === Perform the actual operation (your existing stub) === */
	  json_t *data = json_pack ("{s:i, s:s, s:i}",
				    "port_id", port_id,
				    "commodity", commodity,
				    "quantity", qty);

	  /* Build the final envelope so we can persist exactly what we send */
	  json_t *env = json_object ();
	  json_object_set_new (env, "id", json_string ("srv-trade"));
	  json_object_set (env, "reply_to", json_object_get (root, "id"));
	  char ts[32];
	  iso8601_utc (ts);
	  json_object_set_new (env, "ts", json_string (ts));
	  json_object_set_new (env, "status", json_string ("ok"));
	trade_buy_done:
	  ;
	  json_object_set_new (env, "type", json_string ("trade.accepted"));
	  json_object_set_new (env, "data", data);
	  json_object_set_new (env, "error", json_null ());

	  /* Optional meta: signal replay=false on first-run */
	  json_t *meta = json_object ();
	  if (idem_key && *idem_key)
	    {
	      json_object_set_new (meta, "idempotent_replay", json_false ());
	      json_object_set_new (meta, "idempotency_key",
				   json_string (idem_key));
	    }
	  if (json_object_size (meta) > 0)
	    json_object_set_new (env, "meta", meta);
	  else
	    json_decref (meta);

	  /* If we’re idempotent, store the envelope JSON BEFORE sending */
	  if (idem_key && *idem_key)
	    {
	      char *env_json =
		json_dumps (env, JSON_COMPACT | JSON_SORT_KEYS);
	      if (!env_json
		  || db_idemp_store_response (idem_key,
					      env_json) != SQLITE_OK)
		{
		  if (env_json)
		    free (env_json);
		  json_decref (env);
		  send_enveloped_error (ctx->fd, root, 1500,
					"Database error");
		  goto done_trade_buy;
		}
	      free (env_json);
	    }

	  /* Send */
	  send_all_json (ctx->fd, env);
	  json_decref (env);

	done_trade_buy:
	  (void) 0;
	}
    }
  return 0;
}

static int
json_get_int_field (json_t *obj, const char *key, int *out)
{
  json_t *v = json_object_get (obj, key);
  if (!json_is_integer (v))
    return 0;
  *out = (int) json_integer_value (v);
  return 1;
}

/* ---------- port.info ---------- */
int
cmd_port_info (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  int sector_id = (ctx->sector_id > 0) ? ctx->sector_id : 1;
  json_t *data = json_object_get (root, "data");
  if (json_is_object (data))
    {
      int s;
      if (json_get_int_field (data, "sector_id", &s) && s > 0)
	{
	  sector_id = s;
	}
    }

  /* Fall back to sector-level API and extract {port} */
  json_t *sector = NULL;
  if (db_sector_info_json (sector_id, &sector) != SQLITE_OK || !sector)
    {
      send_enveloped_error (ctx->fd, root, 1500, "Database error");
      return 0;
    }

  json_t *port = json_object_get (sector, "port");
  if (!json_is_object (port))
    {
      json_decref (sector);
      send_enveloped_refused (ctx->fd, root, 1404, "No port in this sector",
			      NULL);
      return 0;
    }

  /* Build payload and send */
  json_t *payload = json_pack ("{s:i s:O}", "sector", sector_id, "port", port);	// port is borrowed; pack copies ref
  send_enveloped_ok (ctx->fd, root, "port.info", payload);
  json_decref (payload);
  json_decref (sector);
  return 0;
}



int
cmd_trade_quote (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }
  send_enveloped_error (ctx->fd, root, 1101, "Not implemented: trade.quote");
  return 0;
}

int
cmd_trade_jettison (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }
  send_enveloped_error (ctx->fd, root, 1101,
			"Not implemented: trade.jettison");
  return 0;
}
