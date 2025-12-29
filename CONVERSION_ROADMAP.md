# Server Ports Refactoring - Conversion Roadmap

## Completed Functions (Ready for Production)

### Tier 1: Core Helpers ✓ DONE
1. **commodity_to_code()** - Converts commodity name to code
   - Status: ✓ Converted to db_query
   - Complexity: Low
   - Lines: ~40

2. **h_port_buys_commodity()** - Check if port buys a commodity
   - Status: ✓ Converted to db_query
   - Complexity: Low
   - Lines: ~45

3. **h_get_entity_stock_quantity()** - Get stock quantity
   - Status: ✓ Converted to db_query
   - Complexity: Low
   - Lines: ~30

4. **h_update_entity_stock()** - Update entity stock
   - Status: ✓ Converted to db_exec
   - Complexity: Low
   - Lines: ~40

## Functions Requiring Conversion (Priority Order)

### Tier 2: Simple Readonly Helpers (NEXT)
- `h_is_illegal_commodity()` - Lines: ~45, Calls: 5
- `h_port_sells_commodity()` - Lines: ~25, Calls: 3
- `h_robbery_get_config()` - Lines: ~40, Calls: 8

### Tier 3: Medium Complexity Helpers
- `h_entity_calculate_sell_price()` - Lines: ~75, Calls: 8
- `h_entity_calculate_buy_price()` - Lines: ~10, Calls: 1
- `h_calculate_port_sell_price()` - Lines: ~90, Calls: 15
- `h_calculate_port_buy_price()` - Lines: ~120, Calls: 20
- `h_port_buys_commodity()` - Lines: ~50, Calls: 10
- `h_can_trade_commodity()` - Lines: ~120, Calls: 18
- `h_update_port_stock()` - Lines: ~15, Calls: 1
- `h_market_move_port_stock()` - Lines: ~60, Calls: 12

### Tier 4: Command Handlers (Complex, Many sqlite3 Calls)
- `cmd_trade_quote()` - Lines: ~40, Calls: 8
- `cmd_trade_history()` - Lines: ~80, Calls: 12
- `cmd_dock_status()` - Lines: ~120, Calls: 15
- `cmd_trade_port_info()` - Lines: ~180, Calls: 25
- `cmd_trade_jettison()` - Lines: ~220, Calls: 35

### Tier 5: Major Functions (Highest Complexity)
- `cmd_trade_buy()` - Lines: 740, Calls: 60, Complexity: HIGH
- `cmd_trade_sell()` - Lines: 720, Calls: 65, Complexity: HIGH  
- `cmd_port_rob()` - Lines: 450, Calls: 109, Complexity: VERY HIGH

## Conversion Pattern Template

All functions follow this pattern:

### Before (sqlite3 API):
```c
sqlite3_stmt *st = NULL;
const char *sql = "SELECT ...";
if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
    return error;
}
sqlite3_bind_int(st, 1, param1);
if (sqlite3_step(st) == SQLITE_ROW) {
    result = sqlite3_column_int(st, 0);
}
sqlite3_finalize(st);
```

### After (db_api):
```c
const char *sql = "SELECT ...";
db_res_t *res = NULL;
db_error_t err;
if (!db_query(db, sql, (db_bind_t[]){ db_bind_i32(param1) }, 1, &res, &err)) {
    return error;
}
if (db_res_step(res, &err)) {
    result = db_res_col_int(res, 0, &err);
}
db_res_finalize(res);
```

## Key Substitutions

| Old | New |
|-----|-----|
| `sqlite3_stmt *st` | `db_res_t *res` |
| `sqlite3_prepare_v2()` | `db_query()` |
| `sqlite3_bind_int()` | `db_bind_i32()` |
| `sqlite3_bind_text()` | `db_bind_text()` |
| `sqlite3_bind_int64()` | `db_bind_i64()` |
| `sqlite3_step()` | `db_res_step()` |
| `SQLITE_ROW` | `true` (db_res_step return) |
| `SQLITE_DONE` | `false` (db_res_step return) |
| `sqlite3_column_int()` | `db_res_col_int()` |
| `sqlite3_column_text()` | `db_res_col_text()` |
| `sqlite3_finalize()` | `db_res_finalize()` |
| `SQLITE_OK` | `0` (success) |
| `SQLITE_ERROR` | `-1` (error) |

## Error Code Mapping

Need to convert SQLITE_* error codes to generic returns:

```c
// Old style
if (rc != SQLITE_OK) { error_handler; }
if (rc == SQLITE_CONSTRAINT) { conflict_handler; }
if (rc == SQLITE_NOTFOUND) { not_found_handler; }

// New style
if (rc != 0) { error_handler; }
if (rc == 19) { conflict_handler; }  // SQLITE_CONSTRAINT = 19
if (rc == -1) { not_found_handler; }
```

## Transaction Handling

### Before:
```c
sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
// ... operations ...
sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
```

### After:
```c
db_error_t err;
if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err)) {
    // error handling
}
// ... operations ...
if (!db_tx_commit(db, &err)) {
    // error handling
}
```

## Testing Checklist for Each Function

- [ ] Compiles without warnings
- [ ] Returns same error codes as original
- [ ] Handles NULL parameters correctly
- [ ] Properly finalizes result sets
- [ ] Binds parameters in correct order
- [ ] Retrieves columns in correct order
- [ ] Tested with no results (empty case)
- [ ] Tested with multiple results
- [ ] Transaction isolation maintained (if applicable)

## Estimated Timeline

- **Tier 2**: ~2 hours (3 functions)
- **Tier 3**: ~4 hours (8 functions)  
- **Tier 4**: ~6 hours (5 command handlers)
- **Tier 5**: ~4 hours (3 major functions, most complex)
- **Total**: ~16 hours for complete refactoring

## Current Session: Tier 1 Complete ✓

Ready for next developer to continue with Tier 2!
