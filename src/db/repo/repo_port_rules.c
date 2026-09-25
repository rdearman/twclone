#include "repo_port_rules.h"
#include "../db_api.h"
#include <stdlib.h>
#include <string.h>

int
repo_port_commodity_rule_get(db_t *db, int porttype_id,
                              const char *commodity_code,
                              porttype_commodity_rule_t *out_rule,
                              bool *out_found)
{
  if (!db || porttype_id <= 0 || !commodity_code || !*commodity_code ||
      !out_rule || !out_found)
    return -1;

  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear(&err);

  *out_found = false;
  memset(out_rule, 0, sizeof(*out_rule));

  /* SQL query with parameterized bindings */
  const char *query =
      "SELECT porttype_commodity_rule_id, porttype_id, commodity_code, "
      "       can_buy, can_sell, base_price_mul, qty_max_mul, "
      "       allow_illegal_override, min_alignment, max_alignment, notes "
      "FROM porttype_commodity_rules "
      "WHERE porttype_id = {1} AND commodity_code = {2} "
      "LIMIT 1;";

  if (db_query(db, query,
               (db_bind_t[]){db_bind_i32(porttype_id),
                             db_bind_text(commodity_code)},
               2, &res, &err) == 0 &&
      db_res_step(res, &err) == 0)
    {
      out_rule->porttype_commodity_rule_id =
          (int)db_res_col_i32(res, 0, &err);
      out_rule->porttype_id = (int)db_res_col_i32(res, 1, &err);

      const char *code = db_res_col_text(res, 2, &err);
      out_rule->commodity_code = code ? (char *)strdup(code) : NULL;

      out_rule->can_buy = (bool)db_res_col_i32(res, 3, &err);
      out_rule->can_sell = (bool)db_res_col_i32(res, 4, &err);
      out_rule->base_price_mul = (int)db_res_col_i32(res, 5, &err);
      out_rule->qty_max_mul = (int)db_res_col_i32(res, 6, &err);

      /* Handle nullable boolean */
      int override_val = (int)db_res_col_i32(res, 7, &err);
      if (err.code == 0 && !db_res_col_is_null(res, 7))
        {
          out_rule->allow_illegal_override = malloc(sizeof(bool));
          *out_rule->allow_illegal_override = (bool)override_val;
        }

      /* Handle nullable ints */
      if (!db_res_col_is_null(res, 8))
        {
          out_rule->min_alignment = malloc(sizeof(int));
          *out_rule->min_alignment = (int)db_res_col_i32(res, 8, &err);
        }

      if (!db_res_col_is_null(res, 9))
        {
          out_rule->max_alignment = malloc(sizeof(int));
          *out_rule->max_alignment = (int)db_res_col_i32(res, 9, &err);
        }

      const char *notes = db_res_col_text(res, 10, &err);
      out_rule->notes = notes ? (char *)strdup(notes) : NULL;

      *out_found = true;
      db_res_finalize(res);
      return 0;
    }

  if (res)
    db_res_finalize(res);
  return -1;
}

int
repo_port_rules_get(db_t *db, int porttype_id,
                    porttype_rule_t *out_rules,
                    bool *out_found)
{
  if (!db || porttype_id <= 0 || !out_rules || !out_found)
    return -1;

  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear(&err);

  *out_found = false;
  memset(out_rules, 0, sizeof(*out_rules));

  const char *query =
      "SELECT porttype_rule_id, porttype_id, allow_illegal, "
      "       min_alignment, max_alignment, notes "
      "FROM porttype_rules "
      "WHERE porttype_id = {1} "
      "LIMIT 1;";

  if (db_query(db, query, (db_bind_t[]){db_bind_i32(porttype_id)}, 1, &res,
               &err) == 0 &&
      db_res_step(res, &err) == 0)
    {
      out_rules->porttype_rule_id = (int)db_res_col_i32(res, 0, &err);
      out_rules->porttype_id = (int)db_res_col_i32(res, 1, &err);
      out_rules->allow_illegal = (bool)db_res_col_i32(res, 2, &err);

      /* Handle nullable ints */
      if (!db_res_col_is_null(res, 3))
        {
          out_rules->min_alignment = malloc(sizeof(int));
          *out_rules->min_alignment = (int)db_res_col_i32(res, 3, &err);
        }

      if (!db_res_col_is_null(res, 4))
        {
          out_rules->max_alignment = malloc(sizeof(int));
          *out_rules->max_alignment = (int)db_res_col_i32(res, 4, &err);
        }

      const char *notes = db_res_col_text(res, 5, &err);
      out_rules->notes = notes ? (char *)strdup(notes) : NULL;

      *out_found = true;
      db_res_finalize(res);
      return 0;
    }

  if (res)
    db_res_finalize(res);
  return -1;
}

int
repo_port_cluster_modifier_get(db_t *db, int porttype_id,
                                int cluster_id,
                                porttype_cluster_modifier_t *out_mod,
                                bool *out_found)
{
  if (!db || porttype_id <= 0 || cluster_id <= 0 || !out_mod || !out_found)
    return -1;

  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear(&err);

  *out_found = false;
  memset(out_mod, 0, sizeof(*out_mod));

  const char *query =
      "SELECT porttype_cluster_modifier_id, porttype_id, cluster_id, "
      "       price_mul, contraband_price_mul, notes "
      "FROM porttype_cluster_modifiers "
      "WHERE porttype_id = {1} AND cluster_id = {2} "
      "LIMIT 1;";

  if (db_query(db, query,
               (db_bind_t[]){db_bind_i32(porttype_id),
                             db_bind_i32(cluster_id)},
               2, &res, &err) == 0 &&
      db_res_step(res, &err) == 0)
    {
      out_mod->porttype_cluster_modifier_id =
          (int)db_res_col_i32(res, 0, &err);
      out_mod->porttype_id = (int)db_res_col_i32(res, 1, &err);
      out_mod->cluster_id = (int)db_res_col_i32(res, 2, &err);
      out_mod->price_mul = (int)db_res_col_i32(res, 3, &err);
      out_mod->contraband_price_mul = (int)db_res_col_i32(res, 4, &err);

      const char *notes = db_res_col_text(res, 5, &err);
      out_mod->notes = notes ? (char *)strdup(notes) : NULL;

      *out_found = true;
      db_res_finalize(res);
      return 0;
    }

  if (res)
    db_res_finalize(res);
  return -1;
}

int
repo_port_effective_price_mul(db_t *db, int porttype_id,
                               int cluster_id)
{
  if (!db || porttype_id <= 0)
    return 100; /* Default 1.0x */

  porttype_commodity_rule_t rule;
  porttype_cluster_modifier_t mod;
  bool found = false;

  /* Get base multiplier from rule (would need commodity_code for full resolution)
   * For now, just return 100 if nothing found */
  int base_mul = 100;

  if (cluster_id > 0)
    {
      if (repo_port_cluster_modifier_get(db, porttype_id, cluster_id, &mod,
                                          &found) == 0 &&
          found)
        {
          /* Apply cluster multiplier */
          base_mul = (base_mul * mod.price_mul) / 100;
          if (mod.notes)
            free(mod.notes);
        }
    }

  return (base_mul > 0) ? base_mul : 100;
}
