-- V3-SHIP-02: keep the sentinel-based tow relationship reciprocal.
-- The server updates both ends in one transaction; a deferred guard permits
-- that transition while rejecting self, orphan and one-sided persisted links.
BEGIN;

CREATE OR REPLACE FUNCTION assert_ship_tow_relationship(p_ship_id integer)
RETURNS void
LANGUAGE plpgsql
AS $$
DECLARE
  v_towing integer;
  v_towed_by integer;
BEGIN
  SELECT towing_ship_id, is_being_towed_by
    INTO v_towing, v_towed_by
    FROM ships
   WHERE ship_id = p_ship_id;
  IF NOT FOUND THEN
    RETURN;
  END IF;

  IF COALESCE(v_towing, 0) = p_ship_id
     OR COALESCE(v_towed_by, 0) = p_ship_id THEN
    RAISE EXCEPTION 'SHIP_TOW_SELF:%', p_ship_id
      USING ERRCODE = '23514';
  END IF;

  IF COALESCE(v_towing, 0) <> 0 THEN
    IF NOT EXISTS (SELECT 1 FROM ships WHERE ship_id = v_towing) THEN
      RAISE EXCEPTION 'SHIP_TOW_TARGET_NOT_FOUND:%', v_towing
        USING ERRCODE = '23503';
    END IF;
    IF COALESCE((SELECT is_being_towed_by FROM ships WHERE ship_id = v_towing), 0)
       <> p_ship_id THEN
      RAISE EXCEPTION 'SHIP_TOW_NOT_RECIPROCAL:%', p_ship_id
        USING ERRCODE = '23514';
    END IF;
  END IF;

  IF COALESCE(v_towed_by, 0) <> 0 THEN
    IF NOT EXISTS (SELECT 1 FROM ships WHERE ship_id = v_towed_by) THEN
      RAISE EXCEPTION 'SHIP_TOW_SOURCE_NOT_FOUND:%', v_towed_by
        USING ERRCODE = '23503';
    END IF;
    IF COALESCE((SELECT towing_ship_id FROM ships WHERE ship_id = v_towed_by), 0)
       <> p_ship_id THEN
      RAISE EXCEPTION 'SHIP_TOW_NOT_RECIPROCAL:%', p_ship_id
        USING ERRCODE = '23514';
    END IF;
  END IF;
END;
$$;

CREATE OR REPLACE FUNCTION enforce_ship_tow_relationship_trigger()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
  -- Constraint triggers run after all statements in the transaction have
  -- settled.  Validate the row's final persisted state; checking endpoint IDs
  -- copied into an earlier queued trigger event can reject a valid multi-step
  -- rewire whose final reciprocal pointers are consistent.
  PERFORM assert_ship_tow_relationship(NEW.ship_id);
  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS ships_tow_relationship_guard ON ships;
CREATE CONSTRAINT TRIGGER ships_tow_relationship_guard
AFTER INSERT OR UPDATE OF towing_ship_id, is_being_towed_by ON ships
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_ship_tow_relationship_trigger();

-- Durable evidence for restart and hazard/tow transaction checks.
CREATE OR REPLACE VIEW ship_tow_invariant_snapshot AS
SELECT
  (SELECT count(*) FROM ships
    WHERE COALESCE(towing_ship_id, 0) = ship_id
       OR COALESCE(is_being_towed_by, 0) = ship_id) AS self_tow_links,
  (SELECT count(*) FROM ships s
    WHERE COALESCE(s.towing_ship_id, 0) <> 0
      AND NOT EXISTS (SELECT 1 FROM ships t WHERE t.ship_id = s.towing_ship_id))
    AS orphan_tow_targets,
  (SELECT count(*) FROM ships s
    WHERE COALESCE(s.is_being_towed_by, 0) <> 0
      AND NOT EXISTS (SELECT 1 FROM ships t WHERE t.ship_id = s.is_being_towed_by))
    AS orphan_tow_sources,
  (SELECT count(*) FROM ships s
    WHERE COALESCE(s.towing_ship_id, 0) <> 0
      AND COALESCE((SELECT t.is_being_towed_by FROM ships t
                    WHERE t.ship_id = s.towing_ship_id), 0) <> s.ship_id)
    AS nonreciprocal_tow_links;

COMMIT;
