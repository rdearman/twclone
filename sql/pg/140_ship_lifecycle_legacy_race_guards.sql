-- V3-SHIP-02: keep upgrade-safe lifecycle fallbacks race-safe.
--
-- Migrations 126/127 deliberately leave old duplicate rows observable instead
-- of failing an upgrade.  In that state the partial unique indexes may not be
-- available, so the deferred guards must serialize the affected aggregate
-- before checking the final row set.
BEGIN;

CREATE OR REPLACE FUNCTION enforce_one_active_player_ship()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
  IF NEW.ship_id IS NOT NULL THEN
    PERFORM pg_advisory_xact_lock(
      hashtextextended('ship-active:' || NEW.ship_id::text, 0)
    );
    IF EXISTS (
      SELECT 1
        FROM players p
       WHERE p.ship_id = NEW.ship_id
         AND p.player_id <> NEW.player_id
    ) THEN
      RAISE EXCEPTION 'SHIP_ACTIVE_DUPLICATE:%', NEW.ship_id
        USING ERRCODE = '23514';
    END IF;
  END IF;
  RETURN NEW;
END;
$$;

CREATE OR REPLACE FUNCTION enforce_one_primary_ship_owner()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
  IF NEW.is_primary IS TRUE THEN
    PERFORM pg_advisory_xact_lock(
      hashtextextended('ship-primary:' || NEW.ship_id::text, 0)
    );
    IF (
      SELECT count(*)
        FROM ship_ownership o
       WHERE o.ship_id = NEW.ship_id
         AND o.is_primary IS TRUE
    ) > 1 THEN
      RAISE EXCEPTION 'SHIP_PRIMARY_OWNER_DUPLICATE:%', NEW.ship_id
        USING ERRCODE = '23505';
    END IF;
  END IF;
  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS ship_ownership_one_primary_guard ON ship_ownership;
CREATE CONSTRAINT TRIGGER ship_ownership_one_primary_guard
AFTER INSERT OR UPDATE OF ship_id, player_id, is_primary ON ship_ownership
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_one_primary_ship_owner();

COMMIT;
