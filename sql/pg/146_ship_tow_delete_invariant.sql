-- V3-SHIP-02: a ship may not be deleted while it participates in a tow link.
--
-- Tow columns are legacy integer references rather than foreign keys.  The
-- deferred reciprocal guard in migration 143 therefore cannot observe a row
-- after it has been deleted.  Require callers to detach both endpoints in
-- the same lifecycle transaction before deleting either ship.
BEGIN;

CREATE OR REPLACE FUNCTION enforce_ship_tow_delete_invariant()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
  IF COALESCE(OLD.towing_ship_id, 0) <> 0
     OR COALESCE(OLD.is_being_towed_by, 0) <> 0 THEN
    RAISE EXCEPTION 'SHIP_TOW_DELETE_REQUIRES_DETACH:%', OLD.ship_id
      USING ERRCODE = '23514';
  END IF;
  RETURN OLD;
END;
$$;

DROP TRIGGER IF EXISTS ships_tow_delete_guard ON ships;
CREATE TRIGGER ships_tow_delete_guard
BEFORE DELETE ON ships
FOR EACH ROW EXECUTE FUNCTION enforce_ship_tow_delete_invariant();

CREATE OR REPLACE VIEW ship_tow_delete_invariant_snapshot AS
SELECT count(*) AS linked_ships_pending_delete
  FROM ships
 WHERE COALESCE(towing_ship_id, 0) <> 0
    OR COALESCE(is_being_towed_by, 0) <> 0;

COMMIT;
