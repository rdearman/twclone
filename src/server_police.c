/*
 * server_police.c — Police interaction RPCs (Phase A stub)
 *
 * Provides:
 *   - police.bribe: jurisdiction-aware stub (Phase A not implemented)
 *   - police.surrender: jurisdiction-aware stub (Phase A not implemented)
 *
 * Phase A behavior:
 *   - Lawless clusters (law_severity == 0): no police presence
 *   - Federation cluster: bribe refused; surrender not implemented
 *   - Other clusters: both not implemented
 *
 * NOTE: This is a Phase A implementation. Full gameplay (bribery probability,
 * sentencing, confiscation, record wipes) is deferred to later phases.
 */

#include <stdio.h>
#include <string.h>
#include <jansson.h>
#include <stdbool.h>
#include "errors.h"
#include "globals.h"
#include "common.h"
#include "server_log.h"
#include "server_envelope.h"
#include "db/db_api.h"
#include "db/sql_driver.h"
#include "db/repo/repo_clusters.h"
#include "game_db.h"

/* ========================================================================
   RPC HANDLER: police.status (Phase D)
   ======================================================================== */

int
handle_police_status (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  json_t *payload = NULL;
  int cluster_id = 0;
  int suspicion = 0;
  int wanted_level = 0;
  int banned = 0;

  if (!ctx || !root)
    return -1;

  if (!db)
    {
      send_response_error (ctx, root, 500, "No database handle.");
      return -1;
    }

  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx, root, ERR_NOT_AUTHENTICATED,
                                   "Not authenticated", NULL);
      return 0;
    }

  /* Resolve cluster_id from request or current sector */
  json_t *data = json_object_get (root, "data");
  if (json_is_object (data))
    {
      json_t *jcluster = json_object_get (data, "cluster_id");
      if (json_is_integer (jcluster))
        {
          cluster_id = (int) json_integer_value (jcluster);
        }
    }

  /* If no cluster_id provided, resolve from current sector */
  if (cluster_id <= 0)
    {
      cluster_info_t info = {0};
      int rc = repo_clusters_get_cluster_info_for_sector (db, ctx->sector_id, &info);
      if (rc == 0)
        {
          cluster_id = info.cluster_id;
        }
      else if (rc == 1)
        {
          /* Unclaimed sector; return default state */
          payload = json_object ();
          json_object_set_new (payload, "cluster_id", json_null ());
          json_object_set_new (payload, "jurisdiction", json_string ("none"));
          json_object_set_new (payload, "suspicion", json_integer (0));
          json_object_set_new (payload, "wanted_level", json_integer (0));
          json_object_set_new (payload, "banned", json_boolean (false));
          send_response_ok_take (ctx, root, "police.status_v1", &payload);
          return 0;
        }
      else
        {
          send_response_error (ctx, root, 500, "Database error resolving cluster.");
          return -1;
        }
    }

  /* Read incident state for this player in this cluster */
  if (repo_clusters_get_player_suspicion_wanted (db, cluster_id, ctx->player_id,
                                                  &suspicion, &wanted_level) != 0)
    {
      send_response_error (ctx, root, 500, "Database error reading incident state.");
      return -1;
    }

  /* Read banned status */
  if (repo_clusters_get_player_banned (db, cluster_id, ctx->player_id, &banned) != 0)
    {
      banned = 0;
    }

  /* Build response */
  payload = json_object ();
  if (!payload)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Out of memory.");
      return -1;
    }

  json_object_set_new (payload, "cluster_id", json_integer (cluster_id));
  json_object_set_new (payload, "suspicion", json_integer (suspicion));
  json_object_set_new (payload, "wanted_level", json_integer (wanted_level));
  json_object_set_new (payload, "banned", json_boolean (banned == 1));

  send_response_ok_take (ctx, root, "police.status_v1", &payload);
  return 0;
}


/* ========================================================================
   RPC HANDLER: police.bribe
   ======================================================================== */

/*
 * cmd_police_bribe()
 *
 * Jurisdiction-aware stub for bribery mechanic.
 *
 * Request payload:
 *   { "amount": <int> }  (optional in Phase A; reserved for Phase B)
 *
 * Response:
 *   On error: standard error response with code + message
 *   On not-implemented: standard error with specific code
 *
 * Rules (Phase A):
 *   - Lawless clusters (law_severity == 0): ERR_NO_POLICE_PRESENCE
 *   - Federation cluster (role == 'FED'): ERR_POLICE_BRIBE_REFUSED
 *   - Other clusters: ERR_NOT_IMPLEMENTED (Phase A stub)
 */
int
cmd_police_bribe(client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle();
  if (!db || !ctx || !root)
    return -1;

  if (ctx->player_id <= 0)
    {
      send_response_error(ctx, root, ERR_NOT_AUTHENTICATED,
                          "Authentication required");
      return 0;
    }

  cluster_info_t cluster_info;
  int lookup_result = repo_clusters_get_cluster_info_for_sector(db, ctx->sector_id,
                                                      &cluster_info);

  if (lookup_result < 0)
    {
      send_response_error(ctx, root, ERR_SERVER_ERROR,
                          "Failed to determine cluster jurisdiction");
      return 0;
    }

  /* Build response object with context info */
  json_t *response_data = json_object();
  json_object_set_new(response_data, "sector_id",
                      json_integer(ctx->sector_id));
  json_object_set_new(response_data, "player_id",
                      json_integer(ctx->player_id));
  json_object_set_new(response_data, "action", json_string("bribe"));

  if (lookup_result == 1)
    {
      /* Sector unclaimed (not in any cluster) */
      json_object_set_new(response_data, "cluster_id", json_integer(0));
      json_object_set_new(response_data, "cluster_role", json_null());
      json_object_set_new(response_data, "law_severity", json_integer(0));

      send_response_error(ctx, root, ERR_NO_POLICE_PRESENCE,
                          "No police presence in this unclaimed sector");
      json_object_clear(response_data);
      json_decref(response_data);
      return 0;
    }

  /* Cluster found */
  json_object_set_new(response_data, "cluster_id",
                      json_integer(cluster_info.cluster_id));
  json_object_set_new(response_data, "cluster_role",
                      json_string(cluster_info.role));
  json_object_set_new(response_data, "law_severity",
                      json_integer(cluster_info.law_severity));

  /* Apply jurisdiction rules */

  if (cluster_info.law_severity == 0)
    {
      /* Lawless cluster: no police */
      send_response_error(ctx, root, ERR_NO_POLICE_PRESENCE,
                          "No police to bribe in lawless space");
      json_object_clear(response_data);
      json_decref(response_data);
      return 0;
    }

  if (strcmp(cluster_info.role, "FED") == 0)
    {
      /* Federation cluster: bribery not allowed */
      send_response_error(ctx, root, ERR_POLICE_BRIBE_REFUSED,
                          "Federation law enforcement does not accept bribes");
      json_object_clear(response_data);
      json_decref(response_data);
      return 0;
    }

  /* Other clusters (RANDOM, etc.): Phase C implementation */
  
  /* Parse bribe amount from request */
  json_t *data = json_object_get(root, "data");
  int bribe_amount = 0;
  if (json_is_object(data))
    {
      json_unpack(data, "{s:i}", "amount", &bribe_amount);
    }
  
  if (bribe_amount <= 0)
    {
      send_response_error(ctx, root, ERR_BAD_REQUEST,
                          "Bribe amount must be positive");
      json_object_clear(response_data);
      json_decref(response_data);
      return 0;
    }

  /* Check if active incident exists */
  int suspicion = 0, wanted_level = 0;
  int incident_active = 0;
  if (repo_clusters_get_player_suspicion_wanted(db, cluster_info.cluster_id,
                                                ctx->player_id,
                                                &suspicion, &wanted_level) == 0)
    {
      if (suspicion > 0 || wanted_level > 0)
        {
          incident_active = 1;
        }
    }
  
  if (!incident_active)
    {
      send_response_error(ctx, root, ERR_NOTHING_TO_SURRENDER,
                          "No active incident to bribe away");
      json_object_clear(response_data);
      json_decref(response_data);
      return 0;
    }

  /* Deterministic bribe success: amount >= 100 * (wanted_level + 1) */
  int success_threshold = 100 * (wanted_level + 1);
  bool bribe_success = (bribe_amount >= success_threshold);

  json_object_set_new(response_data, "bribe_amount", json_integer(bribe_amount));
  json_object_set_new(response_data, "success", json_boolean(bribe_success));
  json_object_set_new(response_data, "suspicion_before", json_integer(suspicion));
  json_object_set_new(response_data, "wanted_level_before", json_integer(wanted_level));

  if (bribe_success)
    {
      /* Apply bribe success: clear suspicion, reduce wanted_level */
      repo_clusters_apply_bribe_success(db, cluster_info.cluster_id, ctx->player_id);
      
      /* Query updated state */
      repo_clusters_get_player_suspicion_wanted(db, cluster_info.cluster_id,
                                                ctx->player_id,
                                                &suspicion, &wanted_level);
      
      json_object_set_new(response_data, "suspicion_after", json_integer(suspicion));
      json_object_set_new(response_data, "wanted_level_after", json_integer(wanted_level));
      json_object_set_new(response_data, "message",
                          json_string("Bribe accepted. Your record has been partially cleared."));
      
      send_response_ok_take(ctx, root, "police.bribe", &response_data);
    }
  else
    {
      /* Apply bribe failure: increment suspicion, possibly promote wanted_level */
      repo_clusters_apply_bribe_failure(db, cluster_info.cluster_id, ctx->player_id);
      
      /* Query updated state */
      repo_clusters_get_player_suspicion_wanted(db, cluster_info.cluster_id,
                                                ctx->player_id,
                                                &suspicion, &wanted_level);
      
      json_object_set_new(response_data, "suspicion_after", json_integer(suspicion));
      json_object_set_new(response_data, "wanted_level_after", json_integer(wanted_level));
      json_object_set_new(response_data, "message",
                          json_string("Bribe rejected. Suspicion increased!"));
      
      send_response_error(ctx, root, 201,
                          "Bribe rejected. Suspicion increased.");
      json_object_clear(response_data);
      json_decref(response_data);
    }

  return 0;
}


/* ========================================================================
   RPC HANDLER: police.surrender
   ======================================================================== */

/*
 * cmd_police_surrender()
 *
 * Jurisdiction-aware stub for surrender mechanic.
 *
 * Request payload:
 *   { }  (optional: "reason" string in Phase B+)
 *
 * Response:
 *   On error: standard error response with code + message
 *   On not-implemented: standard error with specific code
 *
 * Rules (Phase A):
 *   - Lawless clusters (law_severity == 0): ERR_NO_POLICE_PRESENCE
 *   - All other clusters: ERR_NOT_IMPLEMENTED (Phase A stub)
 */
int
cmd_police_surrender(client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle();
  if (!db || !ctx || !root)
    return -1;

  if (ctx->player_id <= 0)
    {
      send_response_error(ctx, root, ERR_NOT_AUTHENTICATED,
                          "Authentication required");
      return 0;
    }

  cluster_info_t cluster_info;
  int lookup_result = repo_clusters_get_cluster_info_for_sector(db, ctx->sector_id,
                                                      &cluster_info);

  if (lookup_result < 0)
    {
      send_response_error(ctx, root, ERR_SERVER_ERROR,
                          "Failed to determine cluster jurisdiction");
      return 0;
    }

  /* Build response object with context info */
  json_t *response_data = json_object();
  json_object_set_new(response_data, "sector_id",
                      json_integer(ctx->sector_id));
  json_object_set_new(response_data, "player_id",
                      json_integer(ctx->player_id));
  json_object_set_new(response_data, "action", json_string("surrender"));

  if (lookup_result == 1)
    {
      /* Sector unclaimed (not in any cluster) */
      json_object_set_new(response_data, "cluster_id", json_integer(0));
      json_object_set_new(response_data, "cluster_role", json_null());
      json_object_set_new(response_data, "law_severity", json_integer(0));

      send_response_error(ctx, root, ERR_NO_POLICE_PRESENCE,
                          "No police to surrender to in unclaimed sector");
      json_object_clear(response_data);
      json_decref(response_data);
      return 0;
    }

  /* Cluster found */
  json_object_set_new(response_data, "cluster_id",
                      json_integer(cluster_info.cluster_id));
  json_object_set_new(response_data, "cluster_role",
                      json_string(cluster_info.role));
  json_object_set_new(response_data, "law_severity",
                      json_integer(cluster_info.law_severity));

  /* Apply jurisdiction rules */

  if (cluster_info.law_severity == 0)
    {
      /* Lawless cluster: no police */
      send_response_error(ctx, root, ERR_NO_POLICE_PRESENCE,
                          "No police in lawless space to surrender to");
      json_object_clear(response_data);
      json_decref(response_data);
      return 0;
    }

  /* Check if active incident exists */
  int suspicion = 0, wanted_level = 0;
  int incident_active = 0;
  if (repo_clusters_get_player_suspicion_wanted(db, cluster_info.cluster_id,
                                                ctx->player_id,
                                                &suspicion, &wanted_level) == 0)
    {
      if (suspicion > 0 || wanted_level > 0)
        {
          incident_active = 1;
        }
    }
  
  if (!incident_active)
    {
      send_response_error(ctx, root, ERR_NOTHING_TO_SURRENDER,
                          "You have nothing to surrender for");
      json_object_clear(response_data);
      json_decref(response_data);
      return 0;
    }

  json_object_set_new(response_data, "suspicion_before", json_integer(suspicion));
  json_object_set_new(response_data, "wanted_level_before", json_integer(wanted_level));

  if (strcmp(cluster_info.role, "FED") == 0)
    {
      /* Federation cluster: full incident wipe */
      repo_clusters_clear_incident_state(db, cluster_info.cluster_id, ctx->player_id);
      
      /* Query updated state */
      repo_clusters_get_player_suspicion_wanted(db, cluster_info.cluster_id,
                                                ctx->player_id,
                                                &suspicion, &wanted_level);
      
      json_object_set_new(response_data, "suspicion_after", json_integer(suspicion));
      json_object_set_new(response_data, "wanted_level_after", json_integer(wanted_level));
      json_object_set_new(response_data, "message",
                          json_string("You have surrendered to the Federation. Your record has been cleared."));
      
      send_response_ok_take(ctx, root, "police.surrender", &response_data);
    }
  else
    {
      /* Local cluster: reduce incident by one tier */
      repo_clusters_reduce_incident_by_tier(db, cluster_info.cluster_id, ctx->player_id);
      
      /* Query updated state */
      repo_clusters_get_player_suspicion_wanted(db, cluster_info.cluster_id,
                                                ctx->player_id,
                                                &suspicion, &wanted_level);
      
      json_object_set_new(response_data, "suspicion_after", json_integer(suspicion));
      json_object_set_new(response_data, "wanted_level_after", json_integer(wanted_level));
      json_object_set_new(response_data, "message",
                          json_string("You have surrendered to local authorities. Your enforcement level has been reduced."));
      
      send_response_ok_take(ctx, root, "police.surrender", &response_data);
    }

  return 0;
}

/* ========================================================================
   PHASE B: Incident Tracking for Crime Actions (e.g., port robbery)
   ======================================================================== */

/*
 * police_track_incident_for_robbery()
 *
 * Track a robbery incident in the local cluster's player status.
 * Called after a successful robbery to increment suspicion.
 *
 * Behavior:
 *   - Lawless (law_severity==0): Do nothing (no police)
 *   - FED cluster, sectors 1-10: Do nothing (Captain Z handles hard punish)
 *   - FED cluster, outside 1-10: Track incident
 *   - Non-FED lawful (law_severity>0): Track incident
 *   - Unclaimed sector: Do nothing
 *
 * Returns:
 *   0 on success
 *   1 if no tracking needed (lawless/unclaimed/FED 1-10)
 *   <0 on DB error
 */
int
police_track_incident_for_robbery(db_t *db, int player_id, int sector_id)
{
  if (!db || player_id <= 0 || sector_id <= 0)
    return -1;

  cluster_info_t cluster_info;
  int rc = repo_clusters_get_cluster_info_for_sector(db, sector_id, &cluster_info);
  
  if (rc == 1)
    /* Unclaimed sector: no cluster, no incident tracking */
    return 1;
  
  if (rc < 0)
    /* DB error */
    return rc;

  /* Lawless clusters: no police presence */
  if (cluster_info.law_severity == 0)
    return 1;

  /* FED cluster in sectors 1-10: Captain Z handles, don't track locally */
  if (sector_id >= 1 && sector_id <= 10 &&
      strcmp(cluster_info.role, "FED") == 0)
    return 1;

  /* All other lawful clusters: track incident (suspicion += 1) */
  if (repo_clusters_get_player_suspicion_wanted(db, cluster_info.cluster_id, 
                                                player_id, NULL, NULL) != 0)
    return -1;

  db_error_t err;
  /* Try to update existing row; insert if needed */
  const char *q_upd = "UPDATE cluster_player_status SET suspicion = suspicion + 1 WHERE cluster_id = {1} AND player_id = {2}";
  char sql_upd[512];
  sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
  int64_t rows = 0;
  
  if (db_exec_rows_affected(db, sql_upd, 
                            (db_bind_t[]){ db_bind_i64(cluster_info.cluster_id),
                                          db_bind_i64(player_id) }, 2, &rows, &err)) {
    if (rows > 0)
      /* Update succeeded, now check if promotion is needed */
      return repo_clusters_promote_wanted_from_suspicion(db, cluster_info.cluster_id, player_id);
  }

  /* No rows updated; try insert */
  const char *q_ins = "INSERT INTO cluster_player_status (cluster_id, player_id, suspicion) VALUES ({1}, {2}, 1)";
  char sql_ins[512];
  sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
  
  if (!db_exec(db, sql_ins, 
               (db_bind_t[]){ db_bind_i64(cluster_info.cluster_id),
                             db_bind_i64(player_id) }, 2, &err)) {
    return err.code;
  }

  return 0;
}

/* EOF server_police.c */
