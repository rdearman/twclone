-- REQUIREMENT_ID: V3-SHIP-02
-- Stable observational evidence for active-fleet settlement transitions.
-- This view deliberately does not repair legacy rows or add a new write guard;
-- callers can compare the row before/after a sale or transfer transaction.
BEGIN;

CREATE OR REPLACE VIEW ship_lifecycle_conservation_snapshot AS
WITH active AS (
  SELECT p.player_id, p.ship_id
    FROM players p
   WHERE p.ship_id IS NOT NULL
), active_ships AS (
  SELECT DISTINCT s.ship_id, s.holds
    FROM active a
    JOIN ships s ON s.ship_id = a.ship_id
), active_owners AS (
  SELECT DISTINCT a.ship_id, a.player_id
    FROM active a
    JOIN ship_ownership o
      ON o.ship_id = a.ship_id
     AND o.player_id = a.player_id
     AND o.is_primary IS TRUE
), legacy_cargo AS (
  SELECT COALESCE(SUM(
           COALESCE(s.colonists, 0) + COALESCE(s.ore, 0) +
           COALESCE(s.organics, 0) + COALESCE(s.equipment, 0) +
           COALESCE(s.slaves, 0) + COALESCE(s.weapons, 0) +
           COALESCE(s.drugs, 0)), 0)::bigint AS units
    FROM active_ships a
    JOIN ships s ON s.ship_id = a.ship_id
), dynamic_cargo AS (
  SELECT COALESCE(SUM(sc.quantity), 0)::bigint AS units
    FROM ship_cargo sc
    JOIN active_ships a ON a.ship_id = sc.ship_id
), over_capacity AS (
  SELECT count(*)::bigint AS violations
    FROM active_ships a
   WHERE (SELECT COALESCE(SUM(sc.quantity), 0)
            FROM ship_cargo sc
           WHERE sc.ship_id = a.ship_id) > a.holds
), owner_mismatch AS (
  SELECT count(*)::bigint AS violations
    FROM active a
   WHERE NOT EXISTS (
     SELECT 1
       FROM ship_ownership o
      WHERE o.ship_id = a.ship_id
        AND o.player_id = a.player_id
        AND o.is_primary IS TRUE
   )
)
SELECT
  (SELECT count(*)::bigint FROM active) AS active_ship_count,
  (SELECT count(*)::bigint FROM active_owners) AS active_primary_owner_count,
  legacy_cargo.units AS active_legacy_cargo_units,
  dynamic_cargo.units AS active_dynamic_cargo_units,
  over_capacity.violations AS active_over_capacity_ships,
  owner_mismatch.violations AS active_owner_mismatches,
  (over_capacity.violations + owner_mismatch.violations)::bigint AS violation_count
FROM legacy_cargo
CROSS JOIN dynamic_cargo
CROSS JOIN over_capacity
CROSS JOIN owner_mismatch;

COMMIT;
