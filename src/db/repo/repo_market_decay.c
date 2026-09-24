#include <stdio.h>
#include <string.h>
#include "repo_market_decay.h"
#include "server_log.h"
#include "db/db_api.h"
#include "repo_cmd.h"  /* for db_get_config_bool/int */

/* ===================================================================
 * Decay port-level market state
 * =================================================================== */
int
repo_market_decay_port_state(db_t *db, int decay_num, int decay_den)
{
  if (!db || decay_den <= 0)
    {
      LOGE("repo_market_decay_port_state: Invalid args (decay_den=%d)", decay_den);
      return -1;
    }

  db_error_t err;
  db_error_clear(&err);

  /* Parameterised SQL: update rolling_volume via integer math */
  const char *sql = 
    "UPDATE port_commodity_state "
    "SET rolling_volume = (rolling_volume * $1 / $2), "
    "    updated_at = CURRENT_TIMESTAMP "
    "WHERE rolling_volume > 0;";

  if (db_exec(db, sql, (db_bind_t[]){
                    db_bind_i32(decay_num),
                    db_bind_i32(decay_den)
                }, 2, &err) != 0)
    {
      LOGE("repo_market_decay_port_state: SQL error");
      return -1;
    }

  LOGI("repo_market_decay_port_state: Decayed port commodity state "
       "(rolling_volume *= %d/%d)", decay_num, decay_den);

  return 0;
}

/* ===================================================================
 * Decay cluster-level market state
 * =================================================================== */
int
repo_market_decay_cluster_state(
    db_t *db,
    int decay_num,
    int decay_den,
    int min_pressure,
    int max_pressure)
{
  if (!db || decay_den <= 0)
    {
      LOGE("repo_market_decay_cluster_state: Invalid args (decay_den=%d)", decay_den);
      return -1;
    }

  db_error_t err;
  db_error_clear(&err);

  /* Parameterised SQL: decay pressure and rolling_volume, then clamp */
  const char *sql = 
    "UPDATE cluster_commodity_pressure "
    "SET pressure = CASE "
    "      WHEN (pressure * $1 / $2) > $3 THEN $3 "
    "      WHEN (pressure * $1 / $2) < $4 THEN $4 "
    "      ELSE (pressure * $1 / $2) "
    "    END, "
    "    rolling_volume = (rolling_volume * $1 / $2), "
    "    updated_at = CURRENT_TIMESTAMP "
    "WHERE pressure != 0 OR rolling_volume > 0;";

  if (db_exec(db, sql, (db_bind_t[]){
                    db_bind_i32(decay_num),
                    db_bind_i32(decay_den),
                    db_bind_i32(max_pressure),
                    db_bind_i32(min_pressure)
                }, 4, &err) != 0)
    {
      LOGE("repo_market_decay_cluster_state: SQL error");
      return -1;
    }

  LOGI("repo_market_decay_cluster_state: Decayed cluster pressure "
       "(pressure *= %d/%d, bounds [%d, %d])", 
       decay_num, decay_den, min_pressure, max_pressure);

  return 0;
}

/* ===================================================================
 * Execute full market decay cycle
 * =================================================================== */
int
repo_market_decay_execute_cycle(
    db_t *db,
    int decay_num,
    int decay_den,
    int min_pressure,
    int max_pressure)
{
  if (!db)
    {
      LOGE("repo_market_decay_execute_cycle: db is NULL");
      return -1;
    }

  /* Check if dynamic pricing is enabled */
  bool dynamic_enabled = db_get_config_bool(db, "market.dynamic_pricing_enabled", false);
  if (!dynamic_enabled)
    {
      LOGD("repo_market_decay_execute_cycle: Skipped (market.dynamic_pricing_enabled=false)");
      return 0;
    }

  /* Decay port state */
  if (repo_market_decay_port_state(db, decay_num, decay_den) != 0)
    {
      LOGE("repo_market_decay_execute_cycle: Failed to decay port state");
      return -1;
    }

  /* Decay cluster state */
  if (repo_market_decay_cluster_state(db, decay_num, decay_den, min_pressure, max_pressure) != 0)
    {
      LOGE("repo_market_decay_execute_cycle: Failed to decay cluster state");
      return -1;
    }

  LOGI("repo_market_decay_execute_cycle: Complete");
  return 0;
}
