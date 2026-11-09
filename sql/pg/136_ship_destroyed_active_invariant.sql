-- V3-SHIP-02: an active player cannot reference a destroyed ship.
-- Keep this cross-table rule deferred so destruction and an active-ship handoff
-- can be coordinated in one transaction.
BEGIN;

CREATE OR REPLACE FUNCTION assert_active_ship_not_destroyed(p_ship_id integer)
RETURNS void
LANGUAGE plpgsql
AS $$
BEGIN
  IF p_ship_id IS NOT NULL AND EXISTS (
    SELECT 1
      FROM players p
      JOIN ships s ON s.ship_id = p.ship_id
     WHERE p.ship_id = p_ship_id
       AND s.destroyed = TRUE
  ) THEN
    RAISE EXCEPTION 'SHIP_ACTIVE_DESTROYED:%', p_ship_id
      USING ERRCODE = '23514';
  END IF;
END;
$$;

CREATE OR REPLACE FUNCTION enforce_active_ship_not_destroyed_trigger()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
  IF TG_OP = 'DELETE' THEN
    PERFORM assert_active_ship_not_destroyed(OLD.ship_id);
    RETURN OLD;
  END IF;

  PERFORM assert_active_ship_not_destroyed(NEW.ship_id);
  IF TG_OP = 'UPDATE' AND OLD.ship_id IS DISTINCT FROM NEW.ship_id THEN
    PERFORM assert_active_ship_not_destroyed(OLD.ship_id);
  END IF;
  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS players_destroyed_ship_guard ON players;
CREATE CONSTRAINT TRIGGER players_destroyed_ship_guard
AFTER INSERT OR UPDATE OF ship_id ON players
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_active_ship_not_destroyed_trigger();

DROP TRIGGER IF EXISTS ships_destroyed_active_guard ON ships;
CREATE CONSTRAINT TRIGGER ships_destroyed_active_guard
AFTER UPDATE OF destroyed ON ships
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_active_ship_not_destroyed_trigger();

COMMIT;
