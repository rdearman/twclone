
/**
 * @brief Retrieves details for a specific commodity at a given port from entity_stock.
 *
 * @param db The SQLite database handle.
 * @param port_id The ID of the port.
 * @param commodity_code The canonical code of the commodity.
 * @param quantity_out Pointer to an integer where the quantity will be stored.
 * @param max_capacity_out Pointer to an integer where the port's max capacity for this commodity will be stored.
 * @param buys_out Pointer to a boolean indicating if the port buys this commodity.
 * @param sells_out Pointer to a boolean indicating if the port sells this commodity.
 * @return SQLITE_OK on success, SQLITE_NOTFOUND if commodity not in stock, or other SQLite error codes.
 */
int
h_get_port_commodity_details (sqlite3 *db,
                              int port_id,
                              const char *commodity_code,
                              int *quantity_out,
                              int *max_capacity_out,
                              bool *buys_out,
                              bool *sells_out)
{
  if (!db || port_id <= 0 || !commodity_code)
    {
      if (quantity_out) *quantity_out = 0;
      if (max_capacity_out) *max_capacity_out = 0;
      if (buys_out) *buys_out = false;
      if (sells_out) *sells_out = false;
      return SQLITE_MISUSE;
    }

  sqlite3_stmt *st = NULL;
  const char *sql =
    "SELECT es.quantity, p.size * 1000 AS max_capacity, "
    "       (CASE WHEN c.code IN ('ORE', 'ORG', 'EQU') THEN 1 ELSE 0 END) AS buys_commodity, " // Simplified assumption: ports buy basic commodities
    "       (CASE WHEN c.code IN ('ORE', 'ORG', 'EQU') THEN 1 ELSE 0 END) AS sells_commodity " // Simplified assumption: ports sell basic commodities
    "FROM ports p "
    "LEFT JOIN entity_stock es ON p.id = es.entity_id AND es.entity_type = 'port' AND es.commodity_code = ?2 "
    "LEFT JOIN commodities c ON es.commodity_code = c.code "
    "WHERE p.id = ?1;";

  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_get_port_commodity_details: prepare failed: %s", sqlite3_errmsg (db));
      if (quantity_out) *quantity_out = 0;
      if (max_capacity_out) *max_capacity_out = 0;
      if (buys_out) *buys_out = false;
      if (sells_out) *sells_out = false;
      return rc;
    }

  sqlite3_bind_int (st, 1, port_id);
  sqlite3_bind_text (st, 2, commodity_code, -1, SQLITE_STATIC);

  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      if (quantity_out) *quantity_out = sqlite3_column_int (st, 0); // Quantity can be NULL if no stock entry
      if (max_capacity_out) *max_capacity_out = sqlite3_column_int (st, 1);
      if (buys_out) *buys_out = (sqlite3_column_int (st, 2) != 0);
      if (sells_out) *sells_out = (sqlite3_column_int (st, 3) != 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      // Port exists, but no stock entry for this commodity (LEFT JOIN)
      // Or port doesn't exist (WHERE p.id = ?1)
      LOGD ("h_get_port_commodity_details: No stock entry found for port %d, commodity %s, or port does not exist.", port_id, commodity_code);
      if (quantity_out) *quantity_out = 0;
      if (max_capacity_out) *max_capacity_out = 0;
      if (buys_out) *buys_out = false;
      if (sells_out) *sells_out = false;
      rc = SQLITE_NOTFOUND;
    }
  else
    {
      LOGE ("h_get_port_commodity_details: step failed: %s", sqlite3_errmsg (db));
      if (quantity_out) *quantity_out = 0;
      if (max_capacity_out) *max_capacity_out = 0;
      if (buys_out) *buys_out = false;
      if (sells_out) *sells_out = false;
      rc = SQLITE_ERROR;
    }

  sqlite3_finalize (st);
  return rc;
}
