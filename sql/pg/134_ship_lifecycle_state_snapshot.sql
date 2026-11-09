-- V3-SHIP-02: durable read-only evidence for active ship lifecycle state.
-- This snapshot complements the write guards in migrations 127/128/132.  It
-- deliberately reports legacy drift instead of rewriting populated data.
BEGIN;

CREATE OR REPLACE VIEW ship_lifecycle_state_snapshot AS
SELECT
  (SELECT count(*)
     FROM (
       SELECT p.ship_id
         FROM players p
        WHERE p.ship_id IS NOT NULL
        GROUP BY p.ship_id
       HAVING count(*) > 1
     ) duplicate_active_ships) AS duplicate_active_ship_references,
  (SELECT count(*)
     FROM players p
    WHERE p.ship_id IS NOT NULL
      AND NOT EXISTS (
        SELECT 1
          FROM ships s
         WHERE s.ship_id = p.ship_id
      )) AS orphan_active_ship_references,
  (SELECT count(*)
     FROM players p
     JOIN ships s ON s.ship_id = p.ship_id
    WHERE p.ship_id IS NOT NULL
      AND s.destroyed = TRUE) AS destroyed_active_ship_references,
  (SELECT count(*)
     FROM players p
     JOIN ships s ON s.ship_id = p.ship_id
    WHERE p.ship_id IS NOT NULL
      AND p.sector_id IS DISTINCT FROM s.sector_id) AS active_sector_mismatches,
  (SELECT count(*)
     FROM players p
    WHERE p.ship_id IS NOT NULL
      AND NOT EXISTS (
        SELECT 1
          FROM ship_ownership o
         WHERE o.ship_id = p.ship_id
           AND o.is_primary = TRUE
      )) AS active_without_primary_owner,
  (SELECT count(*)
     FROM (
       SELECT o.ship_id
         FROM ship_ownership o
        WHERE o.is_primary = TRUE
        GROUP BY o.ship_id
       HAVING count(*) > 1
     ) duplicate_primary_owners) AS duplicate_primary_owners,
  (SELECT count(*)
     FROM ship_ownership o
    WHERE o.is_primary = TRUE
      AND NOT EXISTS (
        SELECT 1
          FROM players p
         WHERE p.player_id = o.player_id
      )) AS orphan_primary_owners;

COMMIT;
