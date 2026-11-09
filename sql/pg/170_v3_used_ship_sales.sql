-- V3-SHIP-01: preserve sold hull identity and sale settlement evidence.
BEGIN;

CREATE TABLE IF NOT EXISTS ship_sale_audit (
    sale_id bigserial PRIMARY KEY,
    ship_id integer NOT NULL REFERENCES ships (ship_id),
    seller_player_id integer NOT NULL REFERENCES players (player_id),
    replacement_ship_id integer NOT NULL REFERENCES ships (ship_id),
    port_id integer NOT NULL REFERENCES ports (port_id),
    hull_valuation bigint NOT NULL CHECK (hull_valuation >= 0),
    equipment_valuation bigint NOT NULL CHECK (equipment_valuation >= 0),
    sale_factor_bp integer NOT NULL CHECK (sale_factor_bp BETWEEN 0 AND 10000),
    proceeds bigint NOT NULL CHECK (proceeds >= 0),
    cargo_forfeited boolean NOT NULL DEFAULT TRUE,
    insurance_terminated boolean NOT NULL DEFAULT TRUE,
    sold_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS shipyard_used_inventory (
    used_inventory_id bigserial PRIMARY KEY,
    sale_id bigint NOT NULL UNIQUE REFERENCES ship_sale_audit (sale_id),
    ship_id integer NOT NULL UNIQUE REFERENCES ships (ship_id),
    port_id integer NOT NULL REFERENCES ports (port_id),
    listed_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_shipyard_used_inventory_port
    ON shipyard_used_inventory (port_id, listed_at, used_inventory_id);

-- Used inventory hulls are unowned by design, but must not be claimed as
-- abandoned ships. Keep this guard inside the same ship-locked claim function
-- so sale-vs-claim races cannot steal an inventory asset.
CREATE OR REPLACE FUNCTION ship_claim(p_player_id bigint, p_ship_id bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
DECLARE
  v_player_sector integer;
  v_ship_sector integer;
  v_destroyed boolean;
  v_type_id integer;
  v_flags bigint;
BEGIN
  SELECT sector_id INTO v_player_sector
    FROM players WHERE player_id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'player not found'::text, NULL::bigint);
  END IF;

  SELECT sector_id, destroyed, type_id, COALESCE(flags, 0)
    INTO v_ship_sector, v_destroyed, v_type_id, v_flags
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
  IF (v_flags & 2) <> 0 THEN
    RETURN (409, 'ship locked'::text, NULL::bigint);
  END IF;
  IF (v_flags & 8) <> 0 THEN
    RETURN (409, 'ship assigned to NPC'::text, NULL::bigint);
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM shiptypes
     WHERE shiptypes_id = v_type_id AND enabled = TRUE AND can_purchase = TRUE
  ) THEN
    RETURN (409, 'ship type is not player-owned'::text, NULL::bigint);
  END IF;
  IF to_regclass('federation_protected_assets') IS NOT NULL
     AND EXISTS (
       SELECT 1 FROM federation_protected_assets
        WHERE asset_kind = 'ship' AND asset_id = p_ship_id AND enabled = TRUE
     ) THEN
    RETURN (409, 'Federation-protected ship'::text, NULL::bigint);
  END IF;
  IF EXISTS (
    SELECT 1 FROM shipyard_used_inventory WHERE ship_id = p_ship_id
  ) THEN
    RETURN (409, 'ship is held in shipyard inventory'::text, NULL::bigint);
  END IF;
  IF EXISTS (SELECT 1 FROM ship_ownership WHERE ship_id = p_ship_id) THEN
    RETURN (409, 'ship already owned'::text, NULL::bigint);
  END IF;

  UPDATE ship_ownership SET is_primary = FALSE
   WHERE player_id = p_player_id AND is_primary = TRUE;
  INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary)
  VALUES (p_ship_id, p_player_id, 1, TRUE);
  UPDATE players SET ship_id = p_ship_id WHERE player_id = p_player_id;
  RETURN (0, 'ok'::text, NULL::bigint);
END;
$$;

COMMIT;
