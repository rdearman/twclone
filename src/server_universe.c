#include <stdio.h>
#include <sqlite3.h>
#include "server_universe.h"
#include "database.h"
#include "server_universe.h"
#include "server_cmds.h"
#include "server_rules.h"



json_t *build_sector_info_json (int sector_id);

// int cmd_move_warp(client_ctx_t *ctx, json_t *root){ STUB_NIY(ctx, root, "move.warp"); }
int
cmd_move_pathfind (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "move.pathfind");
}

int
cmd_move_autopilot_start (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "move.autopilot.start");
}

int
cmd_move_autopilot_stop (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "move.autopilot.stop");
}

int
cmd_move_autopilot_status (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "move.autopilot.status");
}

int
cmd_sector_search (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "sector.search");
}


int
universe_init (void)
{
  sqlite3 *handle = db_get_handle ();	/* <-- accessor */
  sqlite3_stmt *stmt;

  const char *sql = "SELECT COUNT(*) FROM sectors;";
  if (sqlite3_prepare_v2 (handle, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "DB universe_init error: %s\n",
	       sqlite3_errmsg (handle));
      return -1;
    }

  int count = 0;
  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      count = sqlite3_column_int (stmt, 0);
    }
  sqlite3_finalize (stmt);

  if (count == 0)
    {
      fprintf (stderr, "Universe empty, running bigbang...\n");
      return bigbang ();
    }

  return 0;
}

/* Shutdown universe (hook for cleanup) */
void
universe_shutdown (void)
{
  /* At present nothing to do, placeholder for later */
}

/* -------- helpers -------- */

/* Return REFUSED(1402) if no link; otherwise OK.
   Later: SELECT 1 FROM warps WHERE from=? AND to=?; */
decision_t
validate_warp_rule (int from_sector, int to_sector)
{
  if (to_sector <= 0)
    return err (ERR_BAD_REQUEST, "Missing required field");
  if (to_sector == 9999)
    return refused (REF_NO_WARP_LINK, "No warp link");	/* your test case */
  /* TODO: if (no_row_in_warps_table) return refused(REF_NO_WARP_LINK, "No warp link"); */
  return ok ();
}


/* -------- MOVEMENT -------- */

int
cmd_move_describe_sector (client_ctx_t *ctx, json_t *root)
{
  int sector_id = ctx->sector_id > 0 ? ctx->sector_id : 0;
  json_t *jdata = json_object_get (root, "data");
  json_t *jsec =
    json_is_object (jdata) ? json_object_get (jdata, "sector_id") : NULL;
  if (json_is_integer (jsec))
    sector_id = (int) json_integer_value (jsec);
  if (sector_id <= 0)
    sector_id = 1;

  handle_sector_info (ctx->fd, root, sector_id, ctx->player_id);

}

int
cmd_move_warp (client_ctx_t *ctx, json_t *root)
{
  json_t *jdata = json_object_get (root, "data");
  int to = 0;
  if (json_is_object (jdata))
    {
      json_t *jto = json_object_get (jdata, "to_sector_id");
      if (json_is_integer (jto))
	to = (int) json_integer_value (jto);
    }

  // inside handle_move_warp(...) or similar
  decision_t d;
  d = validate_warp_rule (ctx->sector_id, to);

  // decision_t d = validate_warp_rule (ctx->sector_id, to);
  if (d.status == DEC_ERROR)
    {
      send_enveloped_error (ctx->fd, root, d.code, d.message);
    }
  else if (d.status == DEC_REFUSED)
    {
      send_enveloped_refused (ctx->fd, root, d.code, d.message, NULL);
    }
  else
    {
      int from = ctx->sector_id;

      /* Persist new sector for this player */
      int prc = db_player_set_sector (ctx->player_id, to);
      if (prc != SQLITE_OK)
	{
	  send_enveloped_error (ctx->fd, root, 1502,
				"Failed to persist player sector");
	  return 1;
	}

      /* Update session state */
      ctx->sector_id = to;

      /* Reply (include current_sector for clients) */
      json_t *data = json_pack ("{s:i, s:i, s:i, s:i}",
				"player_id", ctx->player_id,
				"from_sector_id", from,
				"to_sector_id", to,
				"current_sector", to);
      if (!data)
	{
	  send_enveloped_error (ctx->fd, root, 1500, "Out of memory");
	  return 1;
	}
      send_enveloped_ok (ctx->fd, root, "move.result", data);
      json_decref (data);
    }

}



/* -------- move.pathfind: BFS path A->B with avoid list -------- */
void
handle_move_pathfind (client_ctx_t *ctx, json_t *root)
{
  if (!ctx)
    return;

  /* Parse request data */
  json_t *data = root ? json_object_get (root, "data") : NULL;

  /* from: null -> current sector */
  int from = (ctx->sector_id > 0) ? ctx->sector_id : 1;
  if (data)
    {
      json_t *jfrom = json_object_get (data, "from");
      if (jfrom && json_is_integer (jfrom))
	from = (int) json_integer_value (jfrom);
      /* if null or missing, keep default */
    }

  /* to: required */
  int to = -1;
  if (data)
    {
      json_t *jto = json_object_get (data, "to");
      if (jto && json_is_integer (jto))
	to = (int) json_integer_value (jto);
    }
  if (to <= 0)
    {
      send_enveloped_error (ctx->fd, root, 1401,
			    "Target sector not specified");
      return;
    }

  /* Build avoid set (optional) */
  /* We’ll size bitsets by max sector id */
  sqlite3 *db = db_get_handle ();
  int max_id = 0;
  sqlite3_stmt *st = NULL;

  pthread_mutex_lock (&db_mutex);
  if (sqlite3_prepare_v2 (db, "SELECT MAX(id) FROM sectors", -1, &st, NULL) ==
      SQLITE_OK && sqlite3_step (st) == SQLITE_ROW)
    {
      max_id = sqlite3_column_int (st, 0);
    }
  if (st)
    {
      sqlite3_finalize (st);
      st = NULL;
    }
  pthread_mutex_unlock (&db_mutex);

  if (max_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, 1401, "No sectors");
      return;
    }

  /* Clamp from/to to valid range quickly */
  if (from <= 0 || from > max_id || to > max_id)
    {
      send_enveloped_error (ctx->fd, root, 1401, "Sector not found");
      return;
    }

  /* allocate simple arrays sized max_id+1 */
  size_t N = (size_t) max_id + 1;
  unsigned char *avoid = (unsigned char *) calloc (N, 1);
  int *prev = (int *) malloc (N * sizeof (int));
  unsigned char *seen = (unsigned char *) calloc (N, 1);
  int *queue = (int *) malloc (N * sizeof (int));

  if (!avoid || !prev || !seen || !queue)
    {
      free (avoid);
      free (prev);
      free (seen);
      free (queue);
      send_enveloped_error (ctx->fd, root, 1500, "Out of memory");
      return;
    }

  /* Fill avoid */
  if (data)
    {
      json_t *javoid = json_object_get (data, "avoid");
      if (javoid && json_is_array (javoid))
	{
	  size_t i, len = json_array_size (javoid);
	  for (i = 0; i < len; ++i)
	    {
	      json_t *v = json_array_get (javoid, i);
	      if (json_is_integer (v))
		{
		  int sid = (int) json_integer_value (v);
		  if (sid > 0 && sid <= max_id)
		    avoid[sid] = 1;
		}
	    }
	}
    }

  /* If target or source is avoided, unreachable */
  if (avoid[to] || avoid[from])
    {
      free (avoid);
      free (prev);
      free (seen);
      free (queue);
      send_enveloped_error (ctx->fd, root, 1406, "Path not found");
      return;
    }

  /* Trivial path */
  if (from == to)
    {
      json_t *steps = json_array ();
      json_array_append_new (steps, json_integer (from));
      json_t *out = json_object ();
      json_object_set_new (out, "steps", steps);
      json_object_set_new (out, "total_cost", json_integer (0));
      send_enveloped_ok (ctx->fd, root, "move.path_v1", out);
      free (avoid);
      free (prev);
      free (seen);
      free (queue);
      return;
    }

  /* Prepare neighbor query once */
  pthread_mutex_lock (&db_mutex);
  int rc = sqlite3_prepare_v2 (db,
			       "SELECT to_sector FROM sector_warps WHERE from_sector = ?1",
			       -1, &st, NULL);
  pthread_mutex_unlock (&db_mutex);

  if (rc != SQLITE_OK || !st)
    {
      free (avoid);
      free (prev);
      free (seen);
      free (queue);
      send_enveloped_error (ctx->fd, root, 1500, "Pathfind init failed");
      return;
    }

  /* BFS */
  for (int i = 0; i <= max_id; ++i)
    prev[i] = -1;
  int qh = 0, qt = 0;
  queue[qt++] = from;
  seen[from] = 1;

  int found = 0;

  while (qh < qt)
    {
      int u = queue[qh++];

      /* fetch neighbors of u */
      pthread_mutex_lock (&db_mutex);
      sqlite3_reset (st);
      sqlite3_clear_bindings (st);
      sqlite3_bind_int (st, 1, u);
      while ((rc = sqlite3_step (st)) == SQLITE_ROW)
	{
	  int v = sqlite3_column_int (st, 0);
	  if (v <= 0 || v > max_id)
	    continue;
	  if (avoid[v] || seen[v])
	    continue;
	  seen[v] = 1;
	  prev[v] = u;
	  queue[qt++] = v;
	  if (v == to)
	    {
	      found = 1;
	      /* still finish stepping rows to keep stmt sane */
	      /* break after unlock */
	    }
	}
      pthread_mutex_unlock (&db_mutex);
      if (found)
	break;
    }

  /* finalize stmt */
  pthread_mutex_lock (&db_mutex);
  sqlite3_finalize (st);
  pthread_mutex_unlock (&db_mutex);

  if (!found)
    {
      free (avoid);
      free (prev);
      free (seen);
      free (queue);
      send_enveloped_error (ctx->fd, root, 1406, "Path not found");
      return;
    }

  /* Reconstruct path */
  json_t *steps = json_array ();
  int cur = to;
  int hops = 0;
  /* backtrack into a simple stack (we can append to a temp C array then JSON) */
  int *stack = (int *) malloc (N * sizeof (int));
  if (!stack)
    {
      free (avoid);
      free (prev);
      free (seen);
      free (queue);
      send_enveloped_error (ctx->fd, root, 1500, "Out of memory");
      return;
    }

  int sp = 0;
  while (cur != -1)
    {
      stack[sp++] = cur;
      if (cur == from)
	break;
      cur = prev[cur];
    }
  /* If we didn’t reach 'from', something’s off */
  if (stack[sp - 1] != from)
    {
      free (stack);
      free (avoid);
      free (prev);
      free (seen);
      free (queue);
      send_enveloped_error (ctx->fd, root, 1406, "Path not found");
      return;
    }
  /* reverse into JSON steps: from .. to */
  for (int i = sp - 1; i >= 0; --i)
    {
      json_array_append_new (steps, json_integer (stack[i]));
    }
  hops = sp - 1;
  free (stack);

  /* Build rootponse */
  json_t *out = json_object ();
  json_object_set_new (out, "steps", steps);
  json_object_set_new (out, "total_cost", json_integer (hops));

  send_enveloped_ok (ctx->fd, root, "move.path_v1", out);

  free (avoid);
  free (prev);
  free (seen);
  free (queue);
}

void
handle_sector_info (int fd, json_t *root, int sector_id, int player_id)
{
  json_t *payload = build_sector_info_json (sector_id);
  if (!payload)
    {
      send_enveloped_error (fd, root, 1500,
			    "Out of memory building sector info");
      return;
    }

  // Add beacon info
  char *btxt = NULL;
  if (db_sector_beacon_text (sector_id, &btxt) == SQLITE_OK && btxt && *btxt)
    {
      json_object_set_new (payload, "beacon", json_string (btxt));
      json_object_set_new (payload, "has_beacon", json_true ());
    }
  else
    {
      json_object_set_new (payload, "beacon", json_null ());
      json_object_set_new (payload, "has_beacon", json_false ());
    }
  free (btxt);

  // Add ships info
  json_t *ships = NULL;
  int rc = db_ships_at_sector_json (player_id, sector_id, &ships);
  if (rc == SQLITE_OK)
    {
      json_object_set_new (payload, "ships", ships ? ships : json_array ());
      json_object_set_new (payload, "ships_count",
			   json_integer (json_array_size (ships)));
    }

  // Add port info
  json_t *ports = NULL;
  int pt = db_ports_at_sector_json (sector_id, &ports);
  if (pt == SQLITE_OK)
    {
      json_object_set_new (payload, "ports", ports ? ports : json_array ());
      json_object_set_new (payload, "ports_count",
			   json_integer (json_array_size (ports)));
    }

  // Add planet info
  json_t *planets = NULL;
  int plt = db_planets_at_sector_json (sector_id, &planets);
  if (plt == SQLITE_OK)
    {
      json_object_set_new (payload, "planets",
			   planets ? planets : json_array ());
      json_object_set_new (payload, "planets_count",
			   json_integer (json_array_size (planets)));
    }

  // Add planet info
  json_t *players = NULL;
  int py = db_players_at_sector_json (sector_id, &players);
  if (py == SQLITE_OK)
    {
      json_object_set_new (payload, "players",
			   players ? players : json_array ());
      json_object_set_new (payload, "players_count",
			   json_integer (json_array_size (players)));
    }


  send_enveloped_ok (fd, root, "sector.info", payload);
  json_decref (payload);
}


/* /\* Build a full sector snapshot for sector.info *\/ */
json_t *
build_sector_info_json (int sector_id)
{
  json_t *root = json_object ();
  if (!root)
    return NULL;

  /* Basic info (id/name) */
  json_t *basic = NULL;
  if (db_sector_basic_json (sector_id, &basic) == SQLITE_OK && basic)
    {
      json_t *sid = json_object_get (basic, "sector_id");
      json_t *name = json_object_get (basic, "name");
      if (sid)
	json_object_set (root, "sector_id", sid);
      if (name)
	json_object_set (root, "name", name);
      json_decref (basic);
    }
  else
    {
      json_object_set_new (root, "sector_id", json_integer (sector_id));
    }

  /* Adjacent warps */
  json_t *adj = NULL;
  if (db_adjacent_sectors_json (sector_id, &adj) == SQLITE_OK && adj)
    {
      json_object_set_new (root, "adjacent", adj);
      json_object_set_new (root, "adjacent_count",
			   json_integer ((int) json_array_size (adj)));
    }
  else
    {
      json_object_set_new (root, "adjacent", json_array ());
      json_object_set_new (root, "adjacent_count", json_integer (0));
    }

/* Ports */
  json_t *ports = NULL;
  if (db_ports_at_sector_json (sector_id, &ports) == SQLITE_OK && ports)
    {
      json_object_set_new (root, "ports", ports);
      json_object_set_new (root, "has_port",
			   json_array_size (ports) >
			   0 ? json_true () : json_false ());
    }
  else
    {
      json_object_set_new (root, "ports", json_array ());
      json_object_set_new (root, "has_port", json_false ());
    }

  /* Players */
  json_t *players = NULL;
  if (db_players_at_sector_json (sector_id, &players) == SQLITE_OK && players)
    {
      json_object_set_new (root, "players", players);
      json_object_set_new (root, "players_count",
			   json_integer ((int) json_array_size (players)));
    }
  else
    {
      json_object_set_new (root, "players", json_array ());
      json_object_set_new (root, "players_count", json_integer (0));
    }

  /* Beacons (always include array) */
  json_t *beacons = NULL;
  if (db_beacons_at_sector_json (sector_id, &beacons) == SQLITE_OK && beacons)
    {
      json_object_set_new (root, "beacons", beacons);
      json_object_set_new (root, "beacons_count",
			   json_integer ((int) json_array_size (beacons)));
    }
  else
    {
      json_object_set_new (root, "beacons", json_array ());
      json_object_set_new (root, "beacons_count", json_integer (0));
    }

  /* Planets */
  json_t *planets = NULL;
  if (db_planets_at_sector_json (sector_id, &planets) == SQLITE_OK && planets)
    {
      json_object_set_new (root, "planets", planets);	/* takes ownership */
      json_object_set_new (root, "has_planet",
			   json_array_size (planets) >
			   0 ? json_true () : json_false ());
      json_object_set_new (root, "planets_count",
			   json_integer ((int) json_array_size (planets)));
    }
  else
    {
      json_object_set_new (root, "planets", json_array ());
      json_object_set_new (root, "has_planet", json_false ());
      json_object_set_new (root, "planets_count", json_integer (0));
    }



  /* Beacons (always include array) */
  // json_t *beacons = NULL;
  if (db_beacons_at_sector_json (sector_id, &beacons) == SQLITE_OK && beacons)
    {
      json_object_set_new (root, "beacons", beacons);
      json_object_set_new (root, "beacons_count",
			   json_integer ((int) json_array_size (beacons)));
    }
  else
    {
      json_object_set_new (root, "beacons", json_array ());
      json_object_set_new (root, "beacons_count", json_integer (0));
    }

  return root;
}

/* -------- move.scan: fast, side-effect-free snapshot (defensive build) -------- */
void
cmd_move_scan (client_ctx_t *ctx, json_t *root)
{
  if (!ctx)
    return;

  /* Resolve sector id (default to 1 if session is unset) */
  int sector_id = (ctx->sector_id > 0) ? ctx->sector_id : 1;
  fprintf (stderr, "[move.scan] sector_id=%d\n", sector_id);

  /* 1) Core snapshot from DB (uses sectors.name/beacon; ports.location; ships.location; planets.sector) */
  json_t *core = NULL;
  if (db_sector_scan_core (sector_id, &core) != SQLITE_OK || !core)
    {
      send_enveloped_error (ctx->fd, root, 1401, "Sector not found");
      return;
    }

  /* 2) Adjacent IDs (array) */
  json_t *adj = NULL;
  if (db_adjacent_sectors_json (sector_id, &adj) != SQLITE_OK || !adj)
    {
      adj = json_array ();	/* never null */
    }

  /* 3) Security flags */
  int in_fed = (sector_id >= 1 && sector_id <= 10);
  int safe_zone = json_integer_value (json_object_get (core, "safe_zone"));	/* 0 with your schema */
  json_t *security = json_object ();
  if (!security)
    {
      json_decref (core);
      json_decref (adj);
      send_enveloped_error (ctx->fd, root, 1500, "OOM");
      return;
    }
  json_object_set_new (security, "fedspace", json_boolean (in_fed));
  json_object_set_new (security, "safe_zone",
		       json_boolean (in_fed ? 1 : (safe_zone != 0)));
  json_object_set_new (security, "combat_locked",
		       json_boolean (in_fed ? 1 : 0));

  /* 4) Port summary (presence only) */
  int port_cnt = json_integer_value (json_object_get (core, "port_count"));
  json_t *port = json_object ();
  if (!port)
    {
      json_decref (core);
      json_decref (adj);
      json_decref (security);
      send_enveloped_error (ctx->fd, root, 1500, "OOM");
      return;
    }
  json_object_set_new (port, "present", json_boolean (port_cnt > 0));
  json_object_set_new (port, "class", json_null ());
  json_object_set_new (port, "stance", json_null ());

  /* 5) Counts object */
  int ships = json_integer_value (json_object_get (core, "ship_count"));
  int planets = json_integer_value (json_object_get (core, "planet_count"));
  json_t *counts = json_object ();
  if (!counts)
    {
      json_decref (core);
      json_decref (adj);
      json_decref (security);
      json_decref (port);
      send_enveloped_error (ctx->fd, root, 1500, "OOM");
      return;
    }
  json_object_set_new (counts, "ships", json_integer (ships));
  json_object_set_new (counts, "planets", json_integer (planets));
  json_object_set_new (counts, "mines", json_integer (0));
  json_object_set_new (counts, "fighters", json_integer (0));

  /* 6) Beacon (string or null) */
  const char *btxt =
    json_string_value (json_object_get (core, "beacon_text"));
  json_t *beacon = (btxt && *btxt) ? json_string (btxt) : json_null ();
  if (!beacon)
    {				/* json_string can OOM */
      beacon = json_null ();
    }

  /* 7) Name */
  const char *name = json_string_value (json_object_get (core, "name"));

  /* 8) Build data object explicitly (no json_pack; no chance of NULL from format mismatch) */
  json_t *data = json_object ();
  if (!data)
    {
      json_decref (core);
      json_decref (adj);
      json_decref (security);
      json_decref (port);
      json_decref (counts);
      if (beacon)
	json_decref (beacon);
      send_enveloped_error (ctx->fd, root, 1500, "OOM");
      return;
    }
  json_object_set_new (data, "sector_id", json_integer (sector_id));
  json_object_set_new (data, "name", json_string (name ? name : "Unknown"));
  json_object_set_new (data, "security", security);	/* transfers ownership */
  json_object_set_new (data, "adjacent", adj);	/* transfers ownership */
  json_object_set_new (data, "port", port);	/* transfers ownership */
  json_object_set_new (data, "counts", counts);	/* transfers ownership */
  json_object_set_new (data, "beacon", beacon);	/* transfers ownership */

  /* Optional debug: confirm non-NULL before sending */
  fprintf (stderr, "[move.scan] built data=%p (sector_id=%d)\n",
	   (void *) data, sector_id);

  /* 9) Send envelope (your send_enveloped_ok steals the 'data' ref via _set_new) */
  send_enveloped_ok (ctx->fd, root, "sector.scan_v1", data);

  /* 10) Clean up */
  json_decref (core);
  /* 'data' members already owned by 'data' -> envelope stole 'data' */
}

int
cmd_sector_set_beacon (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || !root)
    return 1;

  json_t *jdata = json_object_get (root, "data");
  json_t *jsector_id = json_object_get (jdata, "sector_id");
  json_t *jtext = json_object_get (jdata, "text");

  /* Guard 0: schema */
  if (!json_is_integer (jsector_id) || !json_is_string (jtext))
    {
      send_enveloped_error (ctx->fd, root, 1300, "Invalid request schema");
      return 1;
    }

  /* Guard 1: player must be in that sector */
  int req_sector_id = (int) json_integer_value (jsector_id);
  if (ctx->sector_id != req_sector_id)
    {
      send_enveloped_error (ctx->fd, root, 1400,
			    "Player is not in the specified sector.");
      return 1;
    }

  /* Guard 2: FedSpace 1–10 is forbidden */
  if (req_sector_id >= 1 && req_sector_id <= 10)
    {
      send_enveloped_error (ctx->fd, root, 1403,
			    "Cannot set a beacon in FedSpace.");
      return 1;
    }

  /* Guard 3: player must have a beacon on the ship */
  if (!db_player_has_beacon_on_ship (ctx->player_id))
    {
      send_enveloped_error (ctx->fd, root, 1401,
			    "Player does not have a beacon on their ship.");
      return 1;
    }

  /* NOTE: Canon behavior: if a beacon already exists, launching another destroys BOTH.
     So we DO NOT reject here. We only check 'had_beacon' to craft a user message. */
  int had_beacon = db_sector_has_beacon (req_sector_id);

  /* Text length guard (<=80) */
  const char *beacon_text = json_string_value (jtext);
  if (!beacon_text)
    beacon_text = "";
  if ((int) strlen (beacon_text) > 80)
    {
      send_enveloped_error (ctx->fd, root, 1400,
			    "Beacon text is too long (max 80 characters).");
      return 1;
    }

  /* Perform the update:
     - if none existed → set text
     - if one existed  → clear (explode both) */
  int rc = db_sector_set_beacon (req_sector_id, beacon_text);
  if (rc != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 1500,
			    "Database error updating beacon.");
      return 1;
    }

  /* Consume the player's beacon (canon: you used it either way) */
  db_player_decrement_beacon_count (ctx->player_id);

  /* ===== Build sector.info payload (same fields as handle_sector_info) ===== */
  json_t *payload = build_sector_info_json (req_sector_id);
  if (!payload)
    {
      send_enveloped_error (ctx->fd, root, 1500,
			    "Out of memory building sector info");
      return 1;
    }

  /* Beacon text */
  char *btxt = NULL;
  if (db_sector_beacon_text (req_sector_id, &btxt) == SQLITE_OK && btxt
      && *btxt)
    {
      json_object_set_new (payload, "beacon", json_string (btxt));
      json_object_set_new (payload, "has_beacon", json_true ());
    }
  else
    {
      json_object_set_new (payload, "beacon", json_null ());
      json_object_set_new (payload, "has_beacon", json_false ());
    }
  free (btxt);

  /* Ships */
  json_t *ships = NULL;
  rc = db_ships_at_sector_json (ctx->player_id, req_sector_id, &ships);
  if (rc == SQLITE_OK)
    {
      json_object_set_new (payload, "ships", ships ? ships : json_array ());
      json_object_set_new (payload, "ships_count",
			   json_integer ((int) json_array_size (ships)));
    }

  /* Ports */
  json_t *ports = NULL;
  int pt = db_ports_at_sector_json (req_sector_id, &ports);
  if (pt == SQLITE_OK)
    {
      json_object_set_new (payload, "ports", ports ? ports : json_array ());
      json_object_set_new (payload, "ports_count",
			   json_integer ((int) json_array_size (ports)));
    }

  /* Planets */
  json_t *planets = NULL;
  int plt = db_planets_at_sector_json (req_sector_id, &planets);
  if (plt == SQLITE_OK)
    {
      json_object_set_new (payload, "planets",
			   planets ? planets : json_array ());
      json_object_set_new (payload, "planets_count",
			   json_integer ((int) json_array_size (planets)));
    }

  /* Players */
  json_t *players = NULL;
  int py = db_players_at_sector_json (req_sector_id, &players);
  if (py == SQLITE_OK)
    {
      json_object_set_new (payload, "players",
			   players ? players : json_array ());
      json_object_set_new (payload, "players_count",
			   json_integer ((int) json_array_size (players)));
    }

  /* ===== Send envelope with a nice meta.message ===== */
  json_t *env = make_base_envelope (root);
  json_object_set_new (env, "status", json_string ("ok"));
  json_object_set_new (env, "type", json_string ("sector.info"));
  json_object_set_new (env, "data", payload);	/* take ownership */

  json_t *meta = json_object ();
  json_object_set_new (meta, "message",
		       json_string (had_beacon
				    ?
				    "Two marker beacons collided and exploded — the sector now has no beacon."
				    : "Beacon deployed."));
  json_object_set_new (env, "meta", meta);

  attach_rate_limit_meta (env, ctx);
  rl_tick (ctx);
  send_all_json (ctx->fd, env);
  json_decref (env);
  return 0;
}
