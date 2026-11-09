-- V3-SHIP-02: expose the complete ship lifecycle invariant contract.
--
-- Keep the component views and ship_lifecycle_current_snapshot stable for
-- diagnostics and compatibility.  This release boundary also includes the
-- semantic primary-owner checks from migration 148, which are not represented
-- in the older composite violation count.
BEGIN;

CREATE OR REPLACE VIEW ship_lifecycle_release_snapshot AS
SELECT
  (c.invariants
   || jsonb_build_object(
        'owned_ships_without_primary', p.owned_ships_without_primary,
        'null_primary_flags', p.null_primary_flags,
        'duplicate_primary_owners_latest', p.duplicate_primary_owners
      )) AS invariants,
  (c.violation_count
   + p.owned_ships_without_primary
   + p.null_primary_flags
   + p.duplicate_primary_owners)::bigint AS violation_count
FROM ship_lifecycle_current_snapshot c
CROSS JOIN ship_primary_owner_semantics_snapshot p;

COMMIT;
