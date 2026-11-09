-- V3-SHIP-02: keep an active player's ship reference resolvable.
-- NOT VALID preserves upgrade safety for databases that already contain an
-- orphaned reference while enforcing every subsequent insert or update.
BEGIN;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1
      FROM pg_constraint
     WHERE conname = 'players_ship_reference'
       AND conrelid = 'players'::regclass
  ) THEN
    ALTER TABLE players
      ADD CONSTRAINT players_ship_reference
      FOREIGN KEY (ship_id) REFERENCES ships (ship_id)
      NOT VALID;
  END IF;
END $$;

-- Read-only evidence for restart/upgrade checks.  Existing violations remain
-- visible until separately repaired and do not become fabricated successes.
CREATE OR REPLACE VIEW ship_active_reference_invariant_snapshot AS
SELECT
  (SELECT count(*)
     FROM players p
    WHERE p.ship_id IS NOT NULL
      AND NOT EXISTS (
        SELECT 1 FROM ships s WHERE s.ship_id = p.ship_id
      )) AS orphan_active_ship_references;

COMMIT;
