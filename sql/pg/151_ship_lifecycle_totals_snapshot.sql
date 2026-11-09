-- V3-SHIP-02: expose lifecycle totals used by restart and settlement checks.
-- These totals are observational; they do not rewrite legacy rows.  The
-- existing component snapshots report invalid rows, while this view lets an
-- acceptance harness prove that a lifecycle transition conserved the active
-- fleet's cargo and primary ownership population.
BEGIN;

CREATE OR REPLACE VIEW ship_lifecycle_totals_snapshot AS
WITH legacy AS (
  SELECT
    COALESCE(SUM(COALESCE(s.colonists, 0) + COALESCE(s.ore, 0) +
                 COALESCE(s.organics, 0) + COALESCE(s.equipment, 0) +
                 COALESCE(s.slaves, 0) + COALESCE(s.weapons, 0) +
                 COALESCE(s.drugs, 0)), 0)::bigint AS legacy_cargo_units
  FROM ships s
  WHERE EXISTS (SELECT 1 FROM players p WHERE p.ship_id = s.ship_id)
), dynamic AS (
  SELECT COALESCE(SUM(sc.quantity), 0)::bigint AS dynamic_cargo_units
  FROM ship_cargo sc
  WHERE EXISTS (SELECT 1 FROM players p WHERE p.ship_id = sc.ship_id)
), active AS (
  SELECT count(*)::bigint AS active_ship_count
  FROM players
  WHERE ship_id IS NOT NULL
), owned AS (
  SELECT count(DISTINCT ship_id)::bigint AS primary_owned_ship_count
  FROM ship_ownership
  WHERE is_primary IS TRUE
)
SELECT
  active.active_ship_count,
  owned.primary_owned_ship_count,
  dynamic.dynamic_cargo_units,
  legacy.legacy_cargo_units,
  (dynamic.dynamic_cargo_units - legacy.legacy_cargo_units)::bigint
    AS dynamic_minus_legacy_cargo_units,
  (SELECT count(*)::bigint
     FROM ships s
    WHERE EXISTS (SELECT 1 FROM players p WHERE p.ship_id = s.ship_id)
      AND (COALESCE(s.colonists, 0) + COALESCE(s.ore, 0) +
           COALESCE(s.organics, 0) + COALESCE(s.equipment, 0) +
           COALESCE(s.slaves, 0) + COALESCE(s.weapons, 0) +
           COALESCE(s.drugs, 0)) > COALESCE(s.holds, 0))
    AS active_over_capacity_ships,
  (SELECT count(*)::bigint
     FROM players p
    WHERE p.ship_id IS NOT NULL
      AND NOT EXISTS (
        SELECT 1 FROM ship_ownership o
         WHERE o.ship_id = p.ship_id
           AND o.player_id = p.player_id
           AND o.is_primary IS TRUE
      )) AS active_owner_mismatches;

COMMIT;
