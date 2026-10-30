-- A ship can have at most one primary owner. Secondary role rows remain allowed.
BEGIN;
CREATE UNIQUE INDEX IF NOT EXISTS ux_ship_ownership_one_primary
  ON ship_ownership (ship_id) WHERE is_primary = TRUE;
CREATE INDEX IF NOT EXISTS ix_ship_ownership_primary_player
  ON ship_ownership (player_id, ship_id) WHERE is_primary = TRUE;
COMMIT;
