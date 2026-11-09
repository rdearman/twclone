-- REQUIREMENT_ID: V3-SHIP-02
-- Make the settlement and conservation evidence part of one stable, read-only
-- release snapshot.  The view does not repair legacy rows or decide sale
-- pricing; it only makes lifecycle acceptance fail closed when a transition
-- loses active ownership, exceeds holds, or changes the tracked cargo total.
BEGIN;

CREATE OR REPLACE VIEW ship_lifecycle_settlement_release_snapshot AS
SELECT
  (r.invariants || jsonb_build_object(
      'active_ship_count', c.active_ship_count,
      'active_primary_owner_count', c.active_primary_owner_count,
      'active_legacy_cargo_units', c.active_legacy_cargo_units,
      'active_dynamic_cargo_units', c.active_dynamic_cargo_units,
      'active_over_capacity_ships', c.active_over_capacity_ships,
      'active_owner_mismatches', c.active_owner_mismatches,
      'active_tow_link_count', s.active_tow_link_count
   )) AS invariants,
  (r.violation_count
   + c.active_over_capacity_ships
   + c.active_owner_mismatches
   + ABS(c.active_dynamic_cargo_units - c.active_legacy_cargo_units)
   + ABS(c.active_ship_count - c.active_primary_owner_count))::bigint
    AS violation_count
FROM ship_lifecycle_release_snapshot r
CROSS JOIN ship_lifecycle_conservation_snapshot c
CROSS JOIN ship_lifecycle_settlement_snapshot s;

COMMIT;
