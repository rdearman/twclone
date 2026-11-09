-- V3-PLAN-01: durable evidence for planet-landing turn/state invariants.
-- This is intentionally a read-only snapshot.  The command handler owns
-- policy and transaction boundaries; this view makes those boundaries
-- observable to restart and acceptance tests.
BEGIN;

CREATE OR REPLACE VIEW planet_landing_invariant_snapshot AS
SELECT
  (SELECT count(*)
     FROM players p
    WHERE p.lastplanet IS NOT NULL
      AND NOT EXISTS (
        SELECT 1 FROM planets pl WHERE pl.planet_id = p.lastplanet
      )) AS orphan_landed_planets,
  (SELECT count(*)
     FROM players p
     JOIN planets pl ON pl.planet_id = p.lastplanet
    WHERE p.lastplanet IS NOT NULL
      AND p.sector_id IS DISTINCT FROM pl.sector_id) AS remote_landed_planets,
  (SELECT count(*)
     FROM turns
    WHERE turns_remaining < 0) AS negative_turn_balances,
  (SELECT count(*)
     FROM players p
    LEFT JOIN turns t ON t.player_id = p.player_id
    WHERE t.player_id IS NULL) AS players_without_turn_rows;

COMMIT;
