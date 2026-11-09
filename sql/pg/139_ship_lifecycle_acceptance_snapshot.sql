-- V3-SHIP-02: combine the independent lifecycle evidence surfaces into one
-- durable read-only row for restart and acceptance checks.
-- The component snapshots deliberately remain available for diagnosis; this
-- view is the compact zero-violation contract consumed by persistence tests.
BEGIN;

CREATE OR REPLACE VIEW ship_lifecycle_acceptance_snapshot AS
SELECT
  l.resource_violations,
  l.legacy_cargo_violations,
  l.dynamic_cargo_violations,
  l.dynamic_cargo_quantity_violations,
  l.active_ship_duplicates,
  l.active_without_primary_owner,
  l.primary_owner_duplicates,
  l.orphan_primary_owners,
  l.active_sector_mismatches,
  c.negative_cargo_quantities,
  c.over_capacity_ships,
  c.legacy_mirror_mismatches,
  r.active_without_primary_owner AS relationship_owner_violations,
  r.active_sector_mismatches AS relationship_sector_violations,
  a.orphan_active_ship_references,
  s.duplicate_active_ship_references,
  s.orphan_active_ship_references AS state_orphan_active_ship_references,
  s.destroyed_active_ship_references,
  s.active_sector_mismatches AS state_sector_violations,
  s.active_without_primary_owner AS state_owner_violations,
  s.duplicate_primary_owners AS state_duplicate_primary_owners,
  s.orphan_primary_owners AS state_orphan_primary_owners,
  o.active_owner_mismatches,
  so.orphan_ship_ownership,
  so.orphan_player_ownership,
  so.duplicate_primary_owners AS ownership_duplicate_primary_owners
FROM ship_lifecycle_invariant_snapshot l
CROSS JOIN ship_cargo_invariant_snapshot c
CROSS JOIN ship_relationship_invariant_snapshot r
CROSS JOIN ship_active_reference_invariant_snapshot a
CROSS JOIN ship_lifecycle_state_snapshot s
CROSS JOIN ship_active_owner_invariant_snapshot o
CROSS JOIN ship_ownership_invariant_snapshot so;

COMMIT;
