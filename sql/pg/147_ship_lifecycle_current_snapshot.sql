-- V3-SHIP-02: expose the post-146 lifecycle contract as one read-only row.
--
-- Keep the older component and composite views for diagnosis and compatibility,
-- but give restart/concurrency checks a stable entry point that also includes
-- the active-sector and tow-delete evidence added after migration 144.
BEGIN;

CREATE OR REPLACE VIEW ship_lifecycle_current_snapshot AS
SELECT
  (c.invariants
   || jsonb_build_object(
        'active_sector_mismatches_latest', a.active_sector_mismatches,
        'linked_ships_pending_delete', d.linked_ships_pending_delete
      )) AS invariants,
  c.violation_count::bigint AS violation_count
FROM ship_lifecycle_composite_snapshot c
CROSS JOIN ship_active_sector_invariant_snapshot a
CROSS JOIN ship_tow_delete_invariant_snapshot d;

COMMIT;
