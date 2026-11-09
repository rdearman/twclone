-- REQUIREMENT_ID: V3-SHIP-02
-- A ship-ownership row must explicitly identify a primary or secondary role.
-- Keep legacy NULL flags observable, but reject every new NULL write so an
-- ownership transfer cannot create an ambiguous primary-owner state.
BEGIN;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1
      FROM pg_constraint
     WHERE conname = 'ship_ownership_primary_flag_required'
       AND conrelid = 'ship_ownership'::regclass
  ) THEN
    ALTER TABLE ship_ownership
      ADD CONSTRAINT ship_ownership_primary_flag_required
      CHECK (is_primary IS NOT NULL)
      NOT VALID;
  END IF;
END $$;

CREATE OR REPLACE VIEW ship_primary_flag_invariant_snapshot AS
SELECT count(*) AS null_primary_flags
  FROM ship_ownership
 WHERE is_primary IS NULL;

COMMIT;
