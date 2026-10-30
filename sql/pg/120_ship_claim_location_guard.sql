-- Apply to existing PostgreSQL databases after 094. Safe to replay.
CREATE OR REPLACE FUNCTION ship_claim(p_player_id bigint, p_ship_id bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
DECLARE
  v_player_sector integer;
  v_ship_sector integer;
  v_destroyed boolean;
BEGIN
  -- Serialize claims by a player and lock the target before checking whether
  -- another player has acquired it. Do not trust a caller-supplied sector.
  SELECT sector_id INTO v_player_sector
    FROM players WHERE player_id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'player not found'::text, NULL::bigint);
  END IF;

  SELECT sector_id, destroyed INTO v_ship_sector, v_destroyed
    FROM ships WHERE ship_id = p_ship_id FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'ship not found'::text, NULL::bigint);
  END IF;

  IF v_player_sector IS NULL OR v_ship_sector IS DISTINCT FROM v_player_sector THEN
    RETURN (409, 'ship not in player sector'::text, NULL::bigint);
  END IF;
  IF v_destroyed IS DISTINCT FROM FALSE THEN
    RETURN (409, 'ship destroyed'::text, NULL::bigint);
  END IF;
  IF EXISTS (SELECT 1 FROM players WHERE ship_id = p_ship_id AND player_id <> p_player_id) THEN
    RETURN (409, 'ship already active for another player'::text, NULL::bigint);
  END IF;

  UPDATE players SET ship_id = p_ship_id WHERE player_id = p_player_id;
  RETURN (0, 'ok'::text, NULL::bigint);
END;
$$;
