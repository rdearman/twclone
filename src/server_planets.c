// server_planets.c
#include "server_planets.h"
#include "server_rules.h"
#include "common.h"
#include "server_log.h"

int
cmd_planet_genesis (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "planet.genesis");
}

int
cmd_planet_info (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "planet.info");
}

int
cmd_planet_rename (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "planet.rename");
}

int
cmd_planet_land (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "planet.land");
}

int
cmd_planet_launch (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "planet.launch");
}

int
cmd_planet_transfer_ownership (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "planet.transfer_ownership");
}

int
cmd_planet_harvest (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "planet.harvest");
}

int
cmd_planet_deposit (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "planet.deposit");
}

int
cmd_planet_withdraw (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "planet.withdraw");
}

int
h_update_planet_stock (sqlite3 *db, int planet_id,
                       const char *commodity, int delta, int *new_qty_out)
{
  if (!commodity || *commodity == '\0')
    return SQLITE_MISUSE;

  char sql_buf[512];
  int rc;

  // --- Fetch current stock and max capacity ---
  int current_qty = 0;
  int max_capacity = 0;
  sqlite3_stmt *select_stmt = NULL;
  snprintf(sql_buf, sizeof(sql_buf),
           "SELECT quantity, max_capacity FROM planet_goods WHERE planet_id = ?1 AND commodity = ?2;");

  rc = sqlite3_prepare_v2(db, sql_buf, -1, &select_stmt, NULL);
  if (rc != SQLITE_OK) {
    LOGE("h_update_planet_stock: SELECT prepare error: %s", sqlite3_errmsg(db));
    return rc;
  }
  sqlite3_bind_int(select_stmt, 1, planet_id);
  sqlite3_bind_text(select_stmt, 2, commodity, -1, SQLITE_STATIC);

  if (sqlite3_step(select_stmt) == SQLITE_ROW) {
    current_qty = sqlite3_column_int(select_stmt, 0);
    max_capacity = sqlite3_column_int(select_stmt, 1);
  } else {
    sqlite3_finalize(select_stmt);
    return SQLITE_NOTFOUND; // Planet or commodity not found
  }
  sqlite3_finalize(select_stmt);

  // --- Perform C-side checks for underflow/overflow ---
  int potential_new_qty = current_qty + delta;

  if (potential_new_qty < 0) {
    LOGD("h_update_planet_stock: Underflow detected for planet_id=%d, commodity=%s, current_qty=%d, delta=%d", planet_id, commodity, current_qty, delta);
    return SQLITE_CONSTRAINT; // Underflow
  }

  // Only check for overflow if max_capacity is positive (i.e., not unlimited)
  if (max_capacity > 0 && potential_new_qty > max_capacity) {
    LOGD("h_update_planet_stock: Overflow detected for planet_id=%d, commodity=%s, current_qty=%d, delta=%d, max_capacity=%d", planet_id, commodity, current_qty, delta, max_capacity);
    return SQLITE_CONSTRAINT; // Overflow
  }

  // --- Build and execute the UPDATE query ---
  snprintf(sql_buf, sizeof(sql_buf),
    "UPDATE planet_goods "
    "SET quantity = ?3 "
    "WHERE planet_id = ?1 AND commodity = ?2 "
    "RETURNING quantity;"); // RETURNING clause to get the new quantity

  sqlite3_stmt *update_stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql_buf, -1, &update_stmt, NULL);
  if (rc != SQLITE_OK) {
    LOGE("h_update_planet_stock: UPDATE prepare error: %s", sqlite3_errmsg(db));
    return rc;
  }

  sqlite3_bind_int(update_stmt, 1, planet_id);
  sqlite3_bind_text(update_stmt, 2, commodity, -1, SQLITE_STATIC);
  sqlite3_bind_int(update_stmt, 3, potential_new_qty);

  rc = sqlite3_step(update_stmt);

  if (rc == SQLITE_ROW) {
    if (new_qty_out) {
      *new_qty_out = sqlite3_column_int(update_stmt, 0);
    }
    rc = SQLITE_OK;
  } else if (rc == SQLITE_DONE) {
    rc = SQLITE_NOTFOUND; // Should not happen if SELECT found the planet/commodity
  }
  sqlite3_finalize(update_stmt);

  return rc;
}
