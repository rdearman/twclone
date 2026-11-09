-- V3-SHIP-02: reject legacy cargo totals that exceed ship holds.
-- ship_cargo is authoritative for new paths, but the legacy commodity columns
-- remain writable by older lifecycle callers.  Keep both representations
-- bounded until those callers are retired.
BEGIN;

CREATE OR REPLACE FUNCTION enforce_ship_legacy_cargo_capacity()
RETURNS trigger
LANGUAGE plpgsql
AS $$
DECLARE
  v_load bigint;
BEGIN
  v_load := COALESCE(NEW.colonists, 0)
          + COALESCE(NEW.ore, 0)
          + COALESCE(NEW.organics, 0)
          + COALESCE(NEW.equipment, 0)
          + COALESCE(NEW.slaves, 0)
          + COALESCE(NEW.weapons, 0)
          + COALESCE(NEW.drugs, 0);

  IF NEW.holds IS NOT NULL AND v_load > NEW.holds THEN
    RAISE EXCEPTION 'SHIP_LEGACY_CARGO_EXCEEDS_HOLDS:%', NEW.ship_id
      USING ERRCODE = '23514';
  END IF;
  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS ship_legacy_cargo_capacity_guard ON ships;
CREATE CONSTRAINT TRIGGER ship_legacy_cargo_capacity_guard
AFTER INSERT OR UPDATE OF holds, colonists, ore, organics, equipment,
  slaves, weapons, drugs ON ships
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_ship_legacy_cargo_capacity();

-- Read-only evidence for populated databases and restart assertions.  Legacy
-- drift is reported rather than rewritten during migration.
CREATE OR REPLACE VIEW ship_legacy_cargo_capacity_snapshot AS
SELECT count(*) AS over_capacity_ships
  FROM ships s
 WHERE COALESCE(s.colonists, 0)
     + COALESCE(s.ore, 0)
     + COALESCE(s.organics, 0)
     + COALESCE(s.equipment, 0)
     + COALESCE(s.slaves, 0)
     + COALESCE(s.weapons, 0)
     + COALESCE(s.drugs, 0) > COALESCE(s.holds, 0);

COMMIT;
