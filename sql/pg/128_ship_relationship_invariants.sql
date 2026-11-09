-- V3-SHIP-02: enforce the cross-table invariants that PostgreSQL CHECK
-- constraints cannot express: every active ship has one primary owner and an
-- active player occupies the same sector as that player's ship.
BEGIN;

CREATE OR REPLACE FUNCTION assert_ship_lifecycle_relationship(p_ship_id integer)
RETURNS void
LANGUAGE plpgsql
AS $$
BEGIN
  IF p_ship_id IS NULL OR NOT EXISTS (
    SELECT 1 FROM ships WHERE ship_id = p_ship_id
  ) THEN
    RETURN;
  END IF;

  IF EXISTS (SELECT 1 FROM players WHERE ship_id = p_ship_id)
     AND NOT EXISTS (
       SELECT 1
       FROM ship_ownership
       WHERE ship_id = p_ship_id AND is_primary = TRUE
     ) THEN
    RAISE EXCEPTION 'SHIP_ACTIVE_WITHOUT_PRIMARY_OWNER:%', p_ship_id;
  END IF;

  IF EXISTS (
    SELECT 1
    FROM players p
    JOIN ships s ON s.ship_id = p.ship_id
    WHERE p.ship_id = p_ship_id
      AND p.sector_id IS DISTINCT FROM s.sector_id
  ) THEN
    RAISE EXCEPTION 'SHIP_ACTIVE_SECTOR_MISMATCH:%', p_ship_id;
  END IF;
END;
$$;

CREATE OR REPLACE FUNCTION enforce_ship_lifecycle_relationship_trigger()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
  IF TG_OP = 'DELETE' THEN
    PERFORM assert_ship_lifecycle_relationship(OLD.ship_id);
  ELSE
    PERFORM assert_ship_lifecycle_relationship(NEW.ship_id);
    IF TG_OP = 'UPDATE' AND OLD.ship_id IS DISTINCT FROM NEW.ship_id THEN
      PERFORM assert_ship_lifecycle_relationship(OLD.ship_id);
    END IF;
  END IF;
  IF TG_OP = 'DELETE' THEN
    RETURN OLD;
  END IF;
  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS players_ship_relationship_guard ON players;
CREATE CONSTRAINT TRIGGER players_ship_relationship_guard
AFTER INSERT OR UPDATE OF ship_id, sector_id ON players
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_ship_lifecycle_relationship_trigger();

DROP TRIGGER IF EXISTS ships_sector_relationship_guard ON ships;
CREATE CONSTRAINT TRIGGER ships_sector_relationship_guard
AFTER UPDATE OF sector_id ON ships
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_ship_lifecycle_relationship_trigger();

DROP TRIGGER IF EXISTS ownership_ship_relationship_guard ON ship_ownership;
CREATE CONSTRAINT TRIGGER ownership_ship_relationship_guard
AFTER INSERT OR UPDATE OR DELETE ON ship_ownership
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_ship_lifecycle_relationship_trigger();

-- Durable evidence for restart and lifecycle tests. Existing inconsistent rows
-- remain observable; the deferred guards reject new inconsistent transitions.
CREATE OR REPLACE VIEW ship_relationship_invariant_snapshot AS
SELECT
  (SELECT count(*)
   FROM players p
   WHERE p.ship_id IS NOT NULL
     AND NOT EXISTS (
       SELECT 1 FROM ship_ownership o
       WHERE o.ship_id = p.ship_id AND o.is_primary = TRUE
     )) AS active_without_primary_owner,
  (SELECT count(*)
   FROM players p
   JOIN ships s ON s.ship_id = p.ship_id
   WHERE p.sector_id IS DISTINCT FROM s.sector_id) AS active_sector_mismatches;

COMMIT;
