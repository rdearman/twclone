#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <jansson.h>
#include <ctype.h>
#include <string.h>
#include <jansson.h>
#include <sqlite3.h>
/* local inlcudes */
#include "server_config.h"
#include "database.h"
#include "server_loop.h"
#include "errors.h"
#include "config.h"
#include "schemas.h"
#include "server_envelope.h"
#include "server_config.h"
#include "server_log.h"
#include "globals.h" // Include globals.h for xp_align_config_t and g_xp_align declaration
server_config_t g_cfg;
json_t *g_capabilities;
xp_align_config_t g_xp_align; // Define the global instance of xp_align_config_t
/* Provided by your DB module; MUST be defined there (no 'static') */
sqlite3 *g_db = NULL;


/* --------- static helpers (not visible to linker) --------- */
static void
config_set_defaults (void)
{
  memset (&g_cfg, 0, sizeof g_cfg);
  /* --- Legacy "config" table defaults --- */
  g_cfg.turnsperday = 120;
  g_cfg.maxwarps_per_sector = 6;
  g_cfg.startingcredits = 10000000;
  g_cfg.corporation_creation_fee = 1000;
  g_cfg.startingfighters = 10;
  g_cfg.startingholds = 20;
  g_cfg.engine.processinterval = 1;
  g_cfg.autosave = 5;
  g_cfg.max_ports = 200;
  g_cfg.max_planets_per_sector = 6;
  g_cfg.max_total_planets = 300;
  g_cfg.max_citadel_level = 6;
  g_cfg.number_of_planet_types = 8;
  g_cfg.max_ship_name_length = 50;
  g_cfg.ship_type_count = 8;
  g_cfg.hash_length = 128;
  g_cfg.default_nodes = 500;
  g_cfg.buff_size = 1024;
  g_cfg.max_name_length = 50;
  g_cfg.planet_type_count = 8;
  g_cfg.server_port = 1234;
  g_cfg.s2s.tcp_port = 4321;
  g_cfg.bank_alert_threshold_player = 1000000;
  g_cfg.bank_alert_threshold_corp = 5000000;
  g_cfg.genesis_enabled = 1;
  g_cfg.genesis_block_at_cap = 0;
  g_cfg.genesis_navhaz_delta = 0;
  g_cfg.genesis_class_weight_M = 10;
  g_cfg.genesis_class_weight_K = 10;
  g_cfg.genesis_class_weight_O = 10;
  g_cfg.genesis_class_weight_L = 10;
  g_cfg.genesis_class_weight_C = 10;
  g_cfg.genesis_class_weight_H = 10;
  g_cfg.genesis_class_weight_U = 5;
  g_cfg.shipyard_enabled = 1;
  g_cfg.shipyard_trade_in_factor_bp = 5000;
  g_cfg.shipyard_require_cargo_fit = 1;
  g_cfg.shipyard_require_fighters_fit = 1;
  g_cfg.shipyard_require_shields_fit = 1;
  g_cfg.shipyard_require_hardware_compat = 1;
  g_cfg.shipyard_tax_bp = 1000;
  g_cfg.illegal_allowed_neutral = 0;
  /* --- Internal / Other defaults --- */
  g_cfg.engine.tick_ms = 50;
  g_cfg.engine.daily_align_sec = 0;
  g_cfg.batching.event_batch = 128;
  g_cfg.batching.command_batch = 64;
  g_cfg.batching.broadcast_batch = 128;
  g_cfg.priorities.default_command_weight = 100;
  g_cfg.priorities.default_event_weight = 100;
  snprintf (g_cfg.s2s.transport, sizeof g_cfg.s2s.transport, "tcp");
  snprintf (g_cfg.s2s.tcp_host, sizeof g_cfg.s2s.tcp_host, "127.0.0.1");
  g_cfg.s2s.frame_size_limit = 2 * 1024 * 1024;
  g_cfg.safety.connect_ms = 1500;
  g_cfg.safety.handshake_ms = 1500;
  g_cfg.safety.rpc_ms = 5000;
  g_cfg.safety.backoff_initial_ms = 100;
  g_cfg.safety.backoff_max_ms = 2000;
  g_cfg.safety.backoff_factor = 2.0;
  g_cfg.secrets.key_id[0] = '\0';
  g_cfg.secrets.key_len = 0;
  // Limpet Mine Configuration Defaults
  g_cfg.mines.limpet.enabled = true;
  g_cfg.mines.limpet.fedspace_allowed = false;
  g_cfg.mines.limpet.msl_allowed = false;
  g_cfg.mines.limpet.per_sector_cap = 250;
  g_cfg.mines.limpet.max_per_ship = 1;
  g_cfg.mines.limpet.allow_multi_owner = false;
  g_cfg.mines.limpet.limpet_ttl_days = 7;
  g_cfg.mines.limpet.scrub_cost = 5000;
  // Death Configuration Defaults (from design brief)
  g_cfg.death.max_per_day = 2;
  g_cfg.death.xp_loss_flat = 100;
  g_cfg.death.xp_loss_percent = 0;
  snprintf (g_cfg.death.drop_cargo, sizeof (g_cfg.death.drop_cargo), "all");
  snprintf (g_cfg.death.drop_credits_mode,
            sizeof (g_cfg.death.drop_credits_mode), "all_ship");
  g_cfg.death.big_sleep_duration_seconds = 86400;
  g_cfg.death.big_sleep_clear_xp_below = 0;
  snprintf (g_cfg.death.escape_pod_spawn_mode,
            sizeof (g_cfg.death.escape_pod_spawn_mode), "safe_path");
  // Combat Configuration Defaults
  g_cfg.combat.turn_cost = 1;
  g_cfg.combat.base_hit = 1.0;
  g_cfg.combat.offense_coeff = 1.0;
  g_cfg.combat.defense_coeff = 1.0;
  g_cfg.combat.flee.engine_weight = 1.0;
  g_cfg.combat.flee.mass_weight = 1.0;
  // Shield Regen Defaults
  g_cfg.regen.enabled = true;
  g_cfg.regen.shield_rate_pct_per_tick = 0.05;
  g_cfg.regen.tick_seconds = 60;
}


typedef enum {
  CFG_T_INT,
  CFG_T_BOOL,
  CFG_T_STRING,
  CFG_T_DOUBLE
} cfg_type_t;


static int
cfg_parse_double (const char *val_str, const char *type_str, double *out)
{
  if (strcmp (type_str, "double") != 0)
    {
      return -1;
    }
  char *endptr;
  errno = 0;
  double val = strtod (val_str, &endptr);
  if (errno != 0 || endptr == val_str || *endptr != '\0')
    {
      return -1;
    }
  *out = val;
  return 0;
}

static int
cfg_parse_int (const char *val_str, const char *type_str, int *out)
{
  if (strcmp (type_str, "int") != 0 && strcmp(type_str, "bool") != 0)
    {
      return -1;
    }
  char *endptr;


  errno = 0;
  long val = strtol (val_str, &endptr, 10);


  if (errno != 0 || endptr == val_str || *endptr != '\0')
    {
      return -1;
    }
  *out = (int) val;
  return 0;
}


static int
cfg_parse_int64 (const char *val_str, const char *type_str, int64_t *out)
{
  /* Treat as 'int' type from DB perspective, but parse to int64 */
  if (strcmp (type_str, "int") != 0)
    {
      return -1;
    }
  char *endptr;


  errno = 0;
  long long val = strtoll (val_str, &endptr, 10);


  if (errno != 0 || endptr == val_str || *endptr != '\0')
    {
      return -1;
    }
  *out = (int64_t) val;
  return 0;
}


static int
validate_cfg (void)
{
  if (g_cfg.engine.tick_ms < 1)
    {
      LOGE ( "ERROR config: engine.tick_ms must be >= 1 (got %d)\n",
             g_cfg.engine.tick_ms);
      return 0;
    }
  if (strcasecmp (g_cfg.s2s.transport, "uds") != 0
      && strcasecmp (g_cfg.s2s.transport, "tcp") != 0)
    {
      LOGE (
        "ERROR config: s2s.transport must be one of [uds|tcp] (got \"%s\")\n",
        g_cfg.s2s.transport);
      return 0;
    }
  if (!strcasecmp (g_cfg.s2s.transport, "uds")
      && g_cfg.s2s.uds_path[0] == '\0')
    {
      LOGE (
        "ERROR config: s2s.uds_path required when s2s.transport=uds\n");
      return 0;
    }
  if (!strcasecmp (g_cfg.s2s.transport, "tcp") &&
      (g_cfg.s2s.tcp_host[0] == '\0' || g_cfg.s2s.tcp_port <= 0))
    {
      LOGE (
        "ERROR config: s2s.tcp_host/tcp_port required when s2s.transport=tcp\n");
      return 0;
    }
  if (g_cfg.s2s.frame_size_limit > 8 * 1024 * 1024)
    {
      LOGE (
        "ERROR config: s2s.frame_size_limit exceeds 8 MiB (%d)\n",
        g_cfg.s2s.frame_size_limit);
      return 0;
    }
  return 1;
}


/* --------- exported API --------- */
void
print_effective_config_redacted (void)
{
  printf ("INFO config: effective (secrets redacted)\n");
  printf ("{\"engine\":{\"tick_ms\":%d,\"daily_align_sec\":%d},",
          g_cfg.engine.tick_ms, g_cfg.engine.daily_align_sec);
  printf
  (
    "\"batching\":{\"event_batch\":%d,\"command_batch\":%d,\"broadcast_batch\":%d},",
    g_cfg.batching.event_batch,
    g_cfg.batching.command_batch,
    g_cfg.batching.broadcast_batch);
  printf
  (
    "\"priorities\":{\"default_command_weight\":%d,\"default_event_weight\":%d},",
    g_cfg.priorities.default_command_weight,
    g_cfg.priorities.default_event_weight);
  printf
  (
    "\"s2s\":{\"transport\":\"%s\",\"uds_path\":\"%s\",\"tcp_host\":\"%s\",\"tcp_port\":%d,\"frame_size_limit\":%d},",
    g_cfg.s2s.transport,
    g_cfg.s2s.uds_path,
    g_cfg.s2s.tcp_host,
    g_cfg.s2s.tcp_port,
    g_cfg.s2s.frame_size_limit);
  printf ("\"safety\":{\"connect_ms\":%d,\"handshake_ms\":%d,\"rpc_ms\":%d,"
          "\"backoff_initial_ms\":%d,\"backoff_max_ms\":%d,\"backoff_factor\":%.2f},",
          g_cfg.safety.connect_ms,
          g_cfg.safety.handshake_ms,
          g_cfg.safety.rpc_ms,
          g_cfg.safety.backoff_initial_ms,
          g_cfg.safety.backoff_max_ms,
          g_cfg.safety.backoff_factor);
  printf ("\"secrets\":{\"key_id\":\"%s\",\"hmac\":\"********\"}}",
          g_cfg.secrets.key_id[0] ? "********" : "");
  printf (",\"mines\":{\"limpet\":{"
          "\"enabled\":%s,"
          "\"fedspace_allowed\":%s,"
          "\"msl_allowed\":%s,"
          "\"per_sector_cap\":%d,"
          "\"max_per_ship\":%d,"
          "\"allow_multi_owner\":%s,"
          "\"scrub_cost\":%d}}}",
          g_cfg.mines.limpet.enabled ? "true" : "false",
          g_cfg.mines.limpet.fedspace_allowed ? "true" : "false",
          g_cfg.mines.limpet.msl_allowed ? "true" : "false",
          g_cfg.mines.limpet.per_sector_cap,
          g_cfg.mines.limpet.max_per_ship,
          g_cfg.mines.limpet.allow_multi_owner ? "true" : "false",
          g_cfg.mines.limpet.scrub_cost);
  // New death config printing
  printf (",\"death\":{"
          "\"max_per_day\":%d,"
          "\"xp_loss_flat\":%d,"
          "\"xp_loss_percent\":%d,"
          "\"drop_cargo\":\"%s\","
          "\"drop_credits_mode\":\"%s\","
          "\"big_sleep_duration_seconds\":%d,"
          "\"big_sleep_clear_xp_below\":%d,"
          "\"escape_pod_spawn_mode\":\"%s\"}}",
          g_cfg.death.max_per_day,
          g_cfg.death.xp_loss_flat,
          g_cfg.death.xp_loss_percent,
          g_cfg.death.drop_cargo,
          g_cfg.death.drop_credits_mode,
          g_cfg.death.big_sleep_duration_seconds,
          g_cfg.death.big_sleep_clear_xp_below,
          g_cfg.death.escape_pod_spawn_mode);
  printf (",\"combat\":{"
          "\"turn_cost\":%d,"
          "\"base_hit\":%.2f,"
          "\"offense_coeff\":%.2f,"
          "\"defense_coeff\":%.2f}}",
          g_cfg.combat.turn_cost,
          g_cfg.combat.base_hit,
          g_cfg.combat.offense_coeff,
          g_cfg.combat.defense_coeff);
  printf (",\"regen\":{"
          "\"enabled\":%s,"
          "\"rate\":%.2f,"
          "\"tick\":%d}}",
          g_cfg.regen.enabled ? "true" : "false",
          g_cfg.regen.shield_rate_pct_per_tick,
          g_cfg.regen.tick_seconds);
  printf ("\n");
}


int
xp_align_config_reload (sqlite3 *db)
{
  (void)db; // Unused now as we are using defaults until schema supports XP config
  // Set sensible defaults directly since config table doesn't have these keys
  g_xp_align.trade_xp_ratio = 10;
  g_xp_align.ship_destroy_xp_multiplier = 2;
  g_xp_align.illegal_base_align_divisor = 50000;
  g_xp_align.illegal_align_factor_good = 2.0;
  g_xp_align.illegal_align_factor_evil = 0.25;
  LOGI (
    "XP/Alignment config loaded (defaults): trade_xp_ratio=%d, ship_destroy_xp_multiplier=%d, "
    "illegal_base_align_divisor=%d, illegal_align_factor_good=%.2f, illegal_align_factor_evil=%.2f",
    g_xp_align.trade_xp_ratio,
    g_xp_align.ship_destroy_xp_multiplier,
    g_xp_align.illegal_base_align_divisor,
    g_xp_align.illegal_align_factor_good,
    g_xp_align.illegal_align_factor_evil);
  return 0;
}


static void
apply_db (sqlite3 *db)
{
  sqlite3_stmt *st = NULL;
  /* New Key-Value-Type Query */
  const char *sql = "SELECT key, value, type FROM config;";
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      LOGW ("[config] Failed to prepare config query: %s", sqlite3_errmsg (db));
      return;
    }
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      const char *key = (const char *) sqlite3_column_text (st, 0);
      const char *val = (const char *) sqlite3_column_text (st, 1);
      const char *type = (const char *) sqlite3_column_text (st, 2);


      if (!key || !val || !type)
        {
          continue;
        }
      /* --- Map keys to g_cfg fields --- */
      /* Integers */
      if (strcmp (key, "turnsperday") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.turnsperday);
        }
      else if (strcmp (key, "combat.turn_cost") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.combat.turn_cost);
        }
      else if (strcmp (key, "combat.base_hit") == 0)
        {
          cfg_parse_double (val, type, &g_cfg.combat.base_hit);
        }
      else if (strcmp (key, "combat.offense_coeff") == 0)
        {
          cfg_parse_double (val, type, &g_cfg.combat.offense_coeff);
        }
      else if (strcmp (key, "combat.defense_coeff") == 0)
        {
          cfg_parse_double (val, type, &g_cfg.combat.defense_coeff);
        }
      else if (strcmp (key, "combat.flee.engine_weight") == 0)
        {
          cfg_parse_double (val, type, &g_cfg.combat.flee.engine_weight);
        }
      else if (strcmp (key, "combat.flee.mass_weight") == 0)
        {
          cfg_parse_double (val, type, &g_cfg.combat.flee.mass_weight);
        }
      else if (strcmp (key, "regen.enabled") == 0)
        {
          int tmp;
          if (cfg_parse_int (val, type, &tmp) == 0) g_cfg.regen.enabled = tmp;
        }
      else if (strcmp (key, "regen.shield_rate_pct_per_tick") == 0)
        {
          cfg_parse_double (val, type, &g_cfg.regen.shield_rate_pct_per_tick);
        }
      else if (strcmp (key, "regen.tick_seconds") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.regen.tick_seconds);
        }
      else if (strcmp (key, "maxwarps_per_sector") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.maxwarps_per_sector);
        }
      else if (strcmp (key, "startingcredits") == 0)
        {
          cfg_parse_int64 (val, type, &g_cfg.startingcredits);                                            /* int64 */
        }
      else if (strcmp (key, "startingfighters") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.startingfighters);
        }
      else if (strcmp (key, "startingholds") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.startingholds);
        }
      else if (strcmp (key, "processinterval") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.engine.processinterval);
        }
      else if (strcmp (key, "autosave") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.autosave);
        }
      else if (strcmp (key, "max_ports") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.max_ports);
        }
      else if (strcmp (key, "max_planets_per_sector") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.max_planets_per_sector);
        }
      else if (strcmp (key, "max_total_planets") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.max_total_planets);
        }
      else if (strcmp (key, "max_citadel_level") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.max_citadel_level);
        }
      else if (strcmp (key, "number_of_planet_types") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.number_of_planet_types);
        }
      else if (strcmp (key, "max_ship_name_length") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.max_ship_name_length);
        }
      else if (strcmp (key, "ship_type_count") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.ship_type_count);
        }
      else if (strcmp (key, "hash_length") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.hash_length);
        }
      else if (strcmp (key, "default_nodes") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.default_nodes);
        }
      else if (strcmp (key, "buff_size") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.buff_size);
        }
      else if (strcmp (key, "max_name_length") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.max_name_length);
        }
      else if (strcmp (key, "planet_type_count") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.planet_type_count);
        }
      else if (strcmp (key, "server_port") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.server_port);
        }
      else if (strcmp (key, "s2s_port") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.s2s.tcp_port);
        }
      else if (strcmp (key, "bank_alert_threshold_player") == 0)
        {
          cfg_parse_int64 (val, type, &g_cfg.bank_alert_threshold_player);                                                        /* int64 */
        }
      else if (strcmp (key, "bank_alert_threshold_corp") == 0)
        {
          cfg_parse_int64 (val, type, &g_cfg.bank_alert_threshold_corp);                                                      /* int64 */
        }
      else if (strcmp (key, "corporation_creation_fee") == 0)
        {
          cfg_parse_int64 (val, type, &g_cfg.corporation_creation_fee);                                                    /* int64 */
        }
      /* Genesis */
      else if (strcmp (key, "genesis_enabled") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.genesis_enabled);
        }
      else if (strcmp (key, "genesis_block_at_cap") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.genesis_block_at_cap);
        }
      else if (strcmp (key, "genesis_navhaz_delta") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.genesis_navhaz_delta);
        }
      else if (strcmp (key, "genesis_class_weight_M") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.genesis_class_weight_M);
        }
      else if (strcmp (key, "genesis_class_weight_K") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.genesis_class_weight_K);
        }
      else if (strcmp (key, "genesis_class_weight_O") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.genesis_class_weight_O);
        }
      else if (strcmp (key, "genesis_class_weight_L") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.genesis_class_weight_L);
        }
      else if (strcmp (key, "genesis_class_weight_C") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.genesis_class_weight_C);
        }
      else if (strcmp (key, "genesis_class_weight_H") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.genesis_class_weight_H);
        }
      else if (strcmp (key, "genesis_class_weight_U") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.genesis_class_weight_U);
        }
      /* Shipyard */
      else if (strcmp (key, "shipyard_enabled") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.shipyard_enabled);
        }
      else if (strcmp (key, "shipyard_trade_in_factor_bp") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.shipyard_trade_in_factor_bp);
        }
      else if (strcmp (key, "shipyard_require_cargo_fit") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.shipyard_require_cargo_fit);
        }
      else if (strcmp (key, "shipyard_require_fighters_fit") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.shipyard_require_fighters_fit);
        }
      else if (strcmp (key, "shipyard_require_shields_fit") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.shipyard_require_shields_fit);
        }
      else if (strcmp (key, "shipyard_require_hardware_compat") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.shipyard_require_hardware_compat);
        }
      else if (strcmp (key, "shipyard_tax_bp") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.shipyard_tax_bp);
        }
      else if (strcmp (key, "illegal_allowed_neutral") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.illegal_allowed_neutral);
        }
      else if (strcmp (key, "max_cloak_duration") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.death.max_cloak_duration);
        }
      else if (strcmp (key, "neutral_band") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.combat.neutral_band);
        }
      /* --- NEW KEYS --- */
      else if (strcmp (key, "num_sectors") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.num_sectors);
        }
      else if (strcmp (key, "planet_treasury_interest_rate_bps") == 0)
        {
          cfg_parse_int (val, type, &g_cfg.planet_treasury_interest_rate_bps);
        }
      else if (strcmp (key, "bank_min_balance_for_interest") == 0)
        {
          cfg_parse_int64 (val, type, &g_cfg.bank_min_balance_for_interest);
        }
      else if (strcmp (key, "bank_max_daily_interest_per_account") == 0)
        {
          cfg_parse_int64 (val, type, &g_cfg.bank_max_daily_interest_per_account);
        }
      /* Log unknown keys as debug (ignore) */
      else
        {
          LOGD ("[config] Unknown key in DB: '%s' (type=%s, val=%s)",
                key,
                type,
                val);
        }
    }
  sqlite3_finalize (st);
  LOGI ("[config] Configuration loaded from Key-Value-Type table.");
  // Secrets loading (unchanged)
  sqlite3_stmt *ks = NULL;


  if (sqlite3_prepare_v2 (db,
                          "SELECT key_id,key_b64 FROM s2s_keys WHERE active=1 AND is_default_tx=1 LIMIT 1",
                          -1,
                          &ks,
                          NULL) == SQLITE_OK
      && sqlite3_step (ks) == SQLITE_ROW)
    {
      const char *kid = (const char *) sqlite3_column_text (ks, 0);


      snprintf (g_cfg.secrets.key_id, sizeof g_cfg.secrets.key_id, "%s",
                kid ? kid : "");
    }
  sqlite3_finalize (ks);
}


static void
apply_env (void)
{
  /* intentionally unused in v1.0 */
  /* same names as before: TW_ENGINE_TICK_MS, etc. */
}


int
load_eng_config (void)
{
  config_set_defaults ();
  // ensure DB is open/initialised here (you already do schema creation)
  extern sqlite3 *g_db;


  g_db = db_get_handle (); // Initialize g_db with the correct handle
  if (g_db)
    {
      apply_db (g_db);
      xp_align_config_reload (g_db); // Call to load XP/Alignment specific config
    }
  apply_env ();                 // ENV overrides DB
  return validate_cfg ();
}



// exported by server_loop.c
void loop_get_supported_commands (const cmd_desc_t **out_tbl, size_t *out_n);


/*
   static int
   resolve_current_sector_from_info (json_t *info_obj, int fallback)
   {
   if (!json_is_object (info_obj))
    return fallback;

   // Preferred flat field
   json_t *j = json_object_get (info_obj, "current_sector");
   if (json_is_integer (j))
    return (int) json_integer_value (j);

   // Common alternates
   json_t *ship = json_object_get (info_obj, "ship");
   if (json_is_object (ship))
    {
      j = json_object_get (ship, "sector_id");
      if (json_is_integer (j))
        return (int) json_integer_value (j);
    }
   json_t *player = json_object_get (info_obj, "player");
   if (json_is_object (player))
    {
      j = json_object_get (player, "sector_id");
      if (json_is_integer (j))
        return (int) json_integer_value (j);
    }
   return fallback;
   }
 */


/////////////////////////// NEW
static json_t *
make_session_hello_payload (int is_authed, int player_id, int sector_id)
{
  json_t *payload = json_object ();
  json_object_set_new (payload, "protocol_version", json_string ("1.0"));
  json_object_set_new (payload, "server_time_unix",
                       json_integer ((json_int_t) time (NULL)));
  json_object_set_new (payload, "authenticated",
                       is_authed ? json_true () : json_false ());
  if (is_authed)
    {
      json_object_set_new (payload, "player_id", json_integer (player_id));
      json_object_set_new (payload, "current_sector",
                           sector_id >
                           0 ? json_integer (sector_id) : json_null ());
    }
  else
    {
      json_object_set_new (payload, "player_id", json_null ());
      json_object_set_new (payload, "current_sector", json_null ());
    }
  /* NEW: ISO-8601 UTC timestamp for clients that prefer strings */
  char iso[32];


  iso8601_utc (iso);
  json_object_set_new (payload, "server_time", json_string (iso));
  return payload;
}


/* ---------- system.hello ---------- */
int
cmd_system_hello (client_ctx_t *ctx, json_t *root)
{
  // alias to session.hello
  return cmd_session_hello (ctx, root);
}


//////////////////
// Define the handler function
int
cmd_system_capabilities (client_ctx_t *ctx, json_t *root)
{
  send_response_ok(ctx, root, "system.capabilities", json_incref (g_capabilities));
  return 0;
}


/* /\* ---------- system.describe_schema (optional) ---------- *\/ */


/* int */


/* cmd_system_describe_schema (client_ctx_t *ctx, json_t *root) */


/* { */


/*   json_t *jdata = json_object_get (root, "data"); */


/*   const char *key = NULL; */


/*   if (json_is_object (jdata)) */


/*     { */


/*       json_t *jkey = json_object_get (jdata, "key"); */


/*       if (json_is_string (jkey)) */


/*      key = json_string_value (jkey); */


/*     } */


/*   if (!key) */


/*     { */


/*       /\* Return the list of keys we have *\/ */


/*       json_t *data = json_pack ("{s:o}", "available", schema_keys ()); */


/*       send_response_ok(ctx, root, "system.schema_list", data); */


/*       json_decref (data); */


/*     } */


/*   else */


/*     { */


/*       json_t *schema = schema_get (key); */


/*       if (!schema) */


/*      { */


/*        send_response_error(ctx, root, ERR_CURSOR_INVALID, "Schema not found"); */


/*      } */


/*       else */


/*      { */


/*        json_t *data = */


/*          json_pack ("{s:s, s:o}", "key", key, "schema", schema); */


/*        send_response_ok(ctx, root, "system.schema", data); */


/*        json_decref (schema); */


/*        json_decref (data); */


/*      } */


/*     } */


/*   return 0; */


/* } */


/* ---------- session.ping ---------- */
int
cmd_session_ping (client_ctx_t *ctx, json_t *root)
{
  /* Echo back whatever is in data (or {}) */
  json_t *jdata = json_object_get (root, "data");
  json_t *echo =
    json_is_object (jdata) ? json_incref (jdata) : json_object ();
  send_response_ok(ctx, root, "session.pong", echo);
  return 0;
}


int
cmd_session_hello (client_ctx_t *ctx, json_t *root)
{
//   const char *req_id = json_string_value (json_object_get (root, "id"));
  json_t *payload = make_session_hello_payload ((ctx->player_id > 0),
                                                ctx->player_id,
                                                ctx->sector_id);
  // Use ONE helper that builds a proper envelope including reply_to + status.
  // If your send_enveloped_ok doesn't add reply_to, fix it (next section).
  send_response_ok (ctx, root, "session.hello", payload);
  return 0;
}


int
cmd_session_disconnect (client_ctx_t *ctx, json_t *root)
{
  json_t *data = json_pack ("{s:s}", "message", "Goodbye");
  send_response_ok(ctx, root, "system.goodbye", data);
  shutdown (ctx->fd, SHUT_RDWR);
  close (ctx->fd);
  return 0;                     /* or break your per-connection loop appropriately */
}

