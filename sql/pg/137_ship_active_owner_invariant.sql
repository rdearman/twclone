-- V3-SHIP-02: an active player's ship must be their primary-owned ship.
-- This cross-table rule is deferred so an ownership transfer and active-ship
-- handoff can be committed as one lifecycle transaction.
BEGIN;

CREATE OR REPLACE FUNCTION assert_ship_active_owner_relationship(p_ship_id integer)
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
     WHERE p.ship_id = p_ship_id
       AND NOT EXISTS (
         SELECT 1
           FROM ship_ownership o
          WHERE o.ship_id = p_ship_id
            AND o.player_id = p.player_id
            AND o.is_primary = TRUE
       )
  ) THEN
    RAISE EXCEPTION 'SHIP_ACTIVE_OWNER_MISMATCH:%', p_ship_id
      USING ERRCODE = '23514';
  END IF;
END;
$$;

CREATE OR REPLACE FUNCTION enforce_ship_active_owner_relationship_trigger()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
  IF TG_OP = 'DELETE' THEN
    PERFORM assert_ship_active_owner_relationship(OLD.ship_id);
    RETURN OLD;
  END IF;

  PERFORM assert_ship_active_owner_relationship(NEW.ship_id);
  IF TG_OP = 'UPDATE' AND OLD.ship_id IS DISTINCT FROM NEW.ship_id THEN
    PERFORM assert_ship_active_owner_relationship(OLD.ship_id);
  END IF;
  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS players_active_owner_guard ON players;
CREATE CONSTRAINT TRIGGER players_active_owner_guard
AFTER INSERT OR UPDATE OF ship_id ON players
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_ship_active_owner_relationship_trigger();

DROP TRIGGER IF EXISTS ownership_active_owner_guard ON ship_ownership;
CREATE CONSTRAINT TRIGGER ownership_active_owner_guard
AFTER INSERT OR UPDATE OR DELETE ON ship_ownership
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_ship_active_owner_relationship_trigger();

CREATE OR REPLACE VIEW ship_active_owner_invariant_snapshot AS
SELECT
  (SELECT count(*)
     FROM players p
    WHERE p.ship_id IS NOT NULL
      AND NOT EXISTS (
        SELECT 1
          FROM ship_ownership o
         WHERE o.ship_id = p.ship_id
           AND o.player_id = p.player_id
           AND o.is_primary = TRUE
      )) AS active_owner_mismatches;

COMMIT;
