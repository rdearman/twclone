-- V3-SHIP-02: reject negative or NULL dynamic cargo quantities.
-- NOT VALID keeps upgrade safety for legacy rows while enforcing every new
-- insert/update.  ship_cargo_invariant_snapshot remains the evidence surface
-- for pre-existing drift until it is explicitly repaired.
BEGIN;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1
      FROM pg_constraint
     WHERE conname = 'ship_cargo_quantity_nonnegative'
       AND conrelid = 'ship_cargo'::regclass
  ) THEN
    ALTER TABLE ship_cargo
      ADD CONSTRAINT ship_cargo_quantity_nonnegative
      CHECK (quantity IS NOT NULL AND quantity >= 0)
      NOT VALID;
  END IF;
END $$;

COMMIT;
