-- V3-SHIP-02: an active player's ship and player location must agree.
-- The guard is deferred so a movement/lifecycle transaction can update both
-- rows in either order.  Existing drift is reported by the snapshot and is
-- not rewritten by this upgrade-safe migration.
BEGIN;

CREATE OR REPLACE FUNCTION assert_ship_active_sector_relationship(p_ship_id integer)
RETURNS void
LANGUAGE plpgsql
AS $$
BEGIN
  IF p_ship_id IS NULL THEN
    RETURN;
  END IF;

  IF EXISTS (
    SELECT 1
      FROM players p
      JOIN ships s ON s.ship_id = p.ship_id
     WHERE p.ship_id = p_ship_id
       AND p.sector_id IS DISTINCT FROM s.sector_id
  ) THEN
    RAISE EXCEPTION 'SHIP_ACTIVE_SECTOR_MISMATCH:%', p_ship_id
      USING ERRCODE = '23514';
  END IF;
END;
$$;

CREATE OR REPLACE FUNCTION enforce_ship_active_sector_relationship_trigger()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
  IF TG_TABLE_NAME = 'players' THEN
    PERFORM assert_ship_active_sector_relationship(NEW.ship_id);
    IF TG_OP = 'UPDATE' AND OLD.ship_id IS DISTINCT FROM NEW.ship_id THEN
      PERFORM assert_ship_active_sector_relationship(OLD.ship_id);
    END IF;
  ELSE
    PERFORM assert_ship_active_sector_relationship(NEW.ship_id);
  END IF;
  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS players_active_sector_guard ON players;
CREATE CONSTRAINT TRIGGER players_active_sector_guard
AFTER INSERT OR UPDATE OF ship_id, sector_id ON players
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_ship_active_sector_relationship_trigger();

DROP TRIGGER IF EXISTS ships_active_sector_guard ON ships;
CREATE CONSTRAINT TRIGGER ships_active_sector_guard
AFTER INSERT OR UPDATE OF sector_id ON ships
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_ship_active_sector_relationship_trigger();

CREATE OR REPLACE VIEW ship_active_sector_invariant_snapshot AS
SELECT
  (SELECT count(*)
     FROM players p
     JOIN ships s ON s.ship_id = p.ship_id
    WHERE p.ship_id IS NOT NULL
      AND p.sector_id IS DISTINCT FROM s.sector_id)
    AS active_sector_mismatches;

COMMIT;
