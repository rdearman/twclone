-- REQUIREMENT_ID: V3-SHIP-02
-- The ship.list contract is derived from primary ownership.  Keep a stable,
-- read-only snapshot for persistence/restart checks so list population and the
-- per-row active flag cannot silently diverge from the lifecycle tables.
BEGIN;

CREATE OR REPLACE VIEW ship_list_invariant_snapshot AS
WITH primary_rows AS (
  SELECT o.ship_id, o.player_id, s.sector_id, p.ship_id AS active_ship_id
    FROM ship_ownership o
    JOIN ships s ON s.ship_id = o.ship_id
    JOIN players p ON p.player_id = o.player_id
   WHERE o.is_primary IS TRUE
), list_shape AS (
  SELECT
    count(*)::bigint AS list_rows,
    count(*) FILTER (WHERE active_ship_id = ship_id)::bigint
      AS active_list_rows,
    count(*) FILTER (WHERE active_ship_id IS DISTINCT FROM ship_id)::bigint
      AS inactive_list_rows,
    count(DISTINCT ship_id)::bigint AS distinct_list_ships
  FROM primary_rows
), ownership_shape AS (
  SELECT count(*)::bigint AS primary_ownership_rows,
         count(DISTINCT ship_id)::bigint AS distinct_primary_ships
    FROM ship_ownership
   WHERE is_primary IS TRUE
)
SELECT
  ownership_shape.primary_ownership_rows,
  list_shape.active_list_rows,
  list_shape.inactive_list_rows,
  list_shape.distinct_list_ships,
  ownership_shape.distinct_primary_ships,
  (ownership_shape.primary_ownership_rows - list_shape.list_rows)::bigint
    AS missing_list_rows,
  (list_shape.list_rows - list_shape.distinct_list_ships)::bigint
    AS duplicate_list_rows,
  (list_shape.active_list_rows + list_shape.inactive_list_rows
   - list_shape.list_rows)::bigint AS active_flag_row_drift,
  (list_shape.distinct_list_ships
   - ownership_shape.distinct_primary_ships)::bigint AS distinct_ship_drift;

COMMIT;
