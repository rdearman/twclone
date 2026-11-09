-- V3-SHIP-02: expose ownership referential drift for lifecycle evidence.
-- The production schema already has ownership foreign keys; this read-only
-- snapshot also makes legacy/corrupt rows observable in isolated and restart
-- checks without rewriting populated data.
BEGIN;

CREATE OR REPLACE VIEW ship_ownership_invariant_snapshot AS
SELECT
  (SELECT count(*)
     FROM ship_ownership o
    WHERE NOT EXISTS (
      SELECT 1 FROM ships s WHERE s.ship_id = o.ship_id
    )) AS orphan_ship_ownership,
  (SELECT count(*)
     FROM ship_ownership o
    WHERE NOT EXISTS (
      SELECT 1 FROM players p WHERE p.player_id = o.player_id
    )) AS orphan_player_ownership,
  (SELECT count(*)
     FROM (
       SELECT o.ship_id
         FROM ship_ownership o
        WHERE o.is_primary = TRUE
        GROUP BY o.ship_id
       HAVING count(*) > 1
     ) duplicate_primary) AS duplicate_primary_owners;

COMMIT;
