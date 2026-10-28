# V3 database repository integration contract

This note records the database APIs added for planetary and sector-entry
persistence. These functions are database-owned; server command handlers remain
responsible for protocol responses and gameplay event publication.

## Planetary APIs

```c
int repo_citadel_get_construction_state(
    db_t *db, int32_t planet_id, int32_t *level, char *status,
    size_t status_size, int32_t *target_level,
    int64_t *start_time, int64_t *completion_time);

int repo_planets_transfer_fighters(
    db_t *db, int player_id, int ship_id, int planet_id,
    const char *direction, int quantity,
    int *planet_after, int *ship_after);

int db_planets_get_citadel_defenses_ex(
    db_t *db, int planet_id, bool *has_active_defenses,
    int *level, int *shields, int *reaction);

int db_planets_consume_citadel_shields(
    db_t *db, int planet_id, int expected_shields, int amount,
    int *remaining);

int db_planets_get_treasury(db_t *db, int planet_id, long long *balance);
int db_planets_add_treasury(db_t *db, int planet_id, int amount);
int db_planets_deduct_treasury(db_t *db, int planet_id, int amount);
int db_planets_destroy(db_t *db, int player_id, int planet_id);
```

- `repo_citadel_get_construction_state`: 0 on read, `ERR_DB_NOT_FOUND` when
  there is no citadel row, argument/query/backend error otherwise.
- `repo_planets_transfer_fighters`: direction is exactly `ship_to_planet` or
  `planet_to_ship`; 0 on committed transfer; `ERR_PERMISSION_DENIED` when
  owner, corp membership, active ship or landed state is invalid;
  `ERR_DB_CONSTRAINT` when inventory/capacity prerequisites fail;
  `ERR_INVALID_ARG` for malformed input; DB transaction errors propagate.
  It owns its transaction and must not be nested in an outer DB transaction.
- `db_planets_get_citadel_defenses_ex`: 0 means query succeeded, including
  missing citadel/no active defenses (false and zero outputs); negative error
  means query/argument failure. Active means positive shields or reaction.
- `db_planets_consume_citadel_shields`: 0 commits the compare-and-decrement;
  `ERR_DB_BUSY` means the expected value was stale or the debit did not match;
  malformed/negative/oversized quantities return `ERR_INVALID_ARG`. The caller
  must reload and recompute on `ERR_DB_BUSY`, never overwrite the new balance.
- Treasury add/deduct are bounded SQL updates; deduction cannot overdraw.
  Callers that need the updated display balance should read it after the write.
- `db_planets_destroy`: 0 commits; authorization is owner or member of the
  owning corporation; errors leave the transaction rolled back. It detaches
  landed ships, clears `players.lastplanet`, deletes planetary stock, and then
  deletes the planet. Existing FK cascades remove dependent citadel/tax state;
  historical event rows with scalar planet references are retained.

The older `db_planets_get_citadel_defenses()` retains its historical return
contract for existing callsites. New server code should use `_ex` to distinguish
an empty defense state from a failed read.

## Sector-entry APIs

```c
int db_combat_apply_entry_asset_hit(
    db_t *db, int ship_id, int sector_id, int asset_id, int asset_type,
    int expected_hull, int expected_shields, int expected_fighters,
    int new_hull, int new_shields, int new_fighters,
    int quantity_consumed);

int db_combat_attach_limpet_from_asset(
    db_t *db, int ship_id, int owner_id, int asset_id, int64_t created_ts);
```

- `db_combat_apply_entry_asset_hit` locks and validates the ship plus sector
  asset, then persists damage and asset consumption in one transaction. It
  returns `ERR_DB_BUSY` for a stale ship snapshot; reload/recompute and retry
  only if the movement/combat lifecycle still permits it. Invalid input returns
  `ERR_INVALID_ARG`; not-found and DB/transaction errors propagate. The
  function owns its transaction.
- `db_combat_attach_limpet_from_asset` inserts the attachment and consumes the
  owner's limpet asset atomically. Returns 0 on success, 1 when already
  attached or the unique attachment raced, and negative DB/input errors
  otherwise. It owns its transaction.
- `db_combat_decrement_or_delete_asset(db, asset_id, quantity)` consumes the
  requested quantity atomically: exact exhaustion deletes the asset row and
  partial exhaustion decrements it. Returns 0 when consumed and -1 when missing,
  insufficient or query failure.

These transaction-owning APIs cannot be composed inside a caller-owned
transaction until the DB abstraction gains explicit nested transaction or
savepoint support. In particular, the current server entry path must call the
atomic hit operation instead of issuing separately observed damage and asset
updates if it needs duplicate-entry protection. Event publication remains a
server/engine coordination responsibility.
