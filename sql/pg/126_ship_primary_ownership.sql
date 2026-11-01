-- A ship can have at most one primary owner. Secondary role rows remain allowed.
BEGIN;

-- A populated upgrade may contain duplicate legacy primary-owner rows.  Do
-- not make the migration fail before the lifecycle snapshot can report that
-- drift; the deferred guard below still rejects every new duplicate write.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1
      FROM ship_ownership
     WHERE is_primary = TRUE
     GROUP BY ship_id
    HAVING count(*) > 1
  ) THEN
    CREATE UNIQUE INDEX IF NOT EXISTS ux_ship_ownership_one_primary
      ON ship_ownership (ship_id) WHERE is_primary = TRUE;
  END IF;
END $$;

CREATE INDEX IF NOT EXISTS ix_ship_ownership_primary_player
  ON ship_ownership (player_id, ship_id) WHERE is_primary = TRUE;

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
