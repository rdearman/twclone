-- V3 abandoned-hull lifecycle repair.
-- This migration deliberately does not touch faction rosters or ship classes.
BEGIN;

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

CREATE OR REPLACE FUNCTION spawn_initial_fleet()
RETURNS void
LANGUAGE plpgsql
AS $$
DECLARE
  v_st_rec record;
  v_sector_id integer;
  v_name text;
BEGIN
  -- Serialize repeated or concurrent initialization calls so the
  -- ownerless-hull check and insert remain one idempotent operation.
  PERFORM pg_advisory_xact_lock(
    hashtextextended('spawn-initial-fleet', 0)
  );

  IF NOT EXISTS (SELECT 1 FROM npc_shipnames) THEN
    INSERT INTO npc_shipnames (name) VALUES
      ('Kobayashi Maru'), ('Botany Bay'), ('Reliant'), ('Enterprise'),
      ('Heart of Gold'), ('Millennium Falcon'), ('Nostromo'), ('Sulaco'),
      ('Event Horizon'), ('Icarus'), ('Prometheus'), ('Serenity'),
      ('Galactica'), ('Pegasus'), ('Normandy'), ('Pillar of Autumn'),
      ('In Amber Clad'), ('Forward Unto Dawn'), ('Spirit of Fire'),
      ('Infinity'), ('Defiant'), ('Voyager'), ('Discovery'), ('Protostar'),
      ('Daedalus'), ('Odyssey'), ('Equinox'), ('Thunderchild'), ('Yamato'),
      ('Arcadia');
  END IF;

  FOR v_st_rec IN
    SELECT shiptypes_id, initialholds, maxholds, maxfighters, maxshields
      FROM shiptypes
     WHERE enabled = TRUE AND can_purchase = TRUE
  LOOP
    IF EXISTS (
      SELECT 1 FROM ships s
       WHERE s.type_id = v_st_rec.shiptypes_id
         AND COALESCE(s.destroyed, FALSE) = FALSE
         AND NOT EXISTS (SELECT 1 FROM ship_ownership so WHERE so.ship_id = s.ship_id)
    ) THEN
      CONTINUE;
    END IF;

    SELECT sector_id INTO v_sector_id
      FROM sectors WHERE sector_id > 10 ORDER BY random() LIMIT 1;
    IF v_sector_id IS NULL THEN CONTINUE; END IF;
    SELECT name INTO v_name FROM npc_shipnames ORDER BY random() LIMIT 1;

    INSERT INTO ships
      (name, type_id, sector_id, holds, mines, limpets, fighters, genesis,
       detonators, probes, photons, shields, installed_shields, beacons,
       colonists, equipment, organics, ore, slaves, weapons, drugs,
       cloaking_devices, destroyed, hull)
    VALUES
      (v_name, v_st_rec.shiptypes_id, v_sector_id,
       GREATEST(1, LEAST(COALESCE(v_st_rec.initialholds, 1),
                         COALESCE(v_st_rec.maxholds, COALESCE(v_st_rec.initialholds, 1)))),
       0, 0, LEAST(1, COALESCE(v_st_rec.maxfighters, 0)), 0, 0, 0, 0,
       LEAST(1, COALESCE(v_st_rec.maxshields, 0)),
       LEAST(1, COALESCE(v_st_rec.maxshields, 0)),
       0, 0, 0, 0, 0, 0, 0, 0, 0, FALSE, 100);
  END LOOP;
END;
$$;

COMMIT;
