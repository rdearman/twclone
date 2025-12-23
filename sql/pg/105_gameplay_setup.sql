-- 105_gameplay_setup.sql
-- Complex game-state initialization procedures

BEGIN;

-- -----------------------------------------------------------------------------
-- 1) Homeworld Placement
-- -----------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION setup_npc_homeworlds()
RETURNS void LANGUAGE plpgsql AS $$
DECLARE
  v_fer_sector int;
  v_ori_sector int;
BEGIN
  -- Use the longest tunnels view to find a remote sector for Ferringhi
  SELECT exit_sector INTO v_fer_sector FROM longest_tunnels ORDER BY tunnel_length_edges DESC LIMIT 1;
  -- If view is empty, fallback to a distant sector
  IF v_fer_sector IS NULL THEN v_fer_sector := 500; END IF;

  -- Update Ferringhi Homeworld (Planet ID 2 from seeds)
  UPDATE planets SET sector = v_fer_sector WHERE id = 2;

  -- Find another exit for Orion or just pick a random high sector
  SELECT exit_sector INTO v_ori_sector FROM longest_tunnels ORDER BY tunnel_length_edges DESC OFFSET 1 LIMIT 1;
  IF v_ori_sector IS NULL THEN v_ori_sector := 450; END IF;

  -- Update Orion Hideout (Planet ID 3 from seeds)
  UPDATE planets SET sector = v_ori_sector WHERE id = 3;
END;
$$;

-- -----------------------------------------------------------------------------
-- 2) Ferringhi Alliance Setup
-- -----------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION setup_ferringhi_alliance()
RETURNS void LANGUAGE plpgsql AS $$
DECLARE
  v_sector int;
  v_corp_id int;
  v_ship_type_id int;
  v_player_id int;
  v_ship_id int;
  v_i int;
BEGIN
  SELECT sector INTO v_sector FROM planets WHERE id = 2;
  SELECT id INTO v_corp_id FROM corporations WHERE tag = 'FENG';
  SELECT id INTO v_ship_type_id FROM shiptypes WHERE name = 'Merchant Freighter';

  -- Create Citadel for Ferringhi Homeworld
  INSERT INTO citadels (planet_id, level, shields, treasury, military, construction_status)
  VALUES (2, 2, 1500, 5000000, 1, 'idle') ON CONFLICT DO NOTHING;

  -- Create 5 Traders
  FOR v_i IN 1..5 LOOP
    INSERT INTO players (name, passwd, credits, sector, type, is_npc, loggedin, commission)
    VALUES (format('Ferrengi Trader %s', v_i), 'BOT', 5000, v_sector, 1, 1, 1, 1)
    RETURNING id INTO v_player_id;

    INSERT INTO ships (name, type_id, sector, fighters, shields, holds, onplanet, perms)
    VALUES ('Ferrengi Trader', v_ship_type_id, v_sector, 1000, 2000, 500, 1, 777)
    RETURNING id INTO v_ship_id;

    UPDATE players SET ship = v_ship_id WHERE id = v_player_id;
    INSERT INTO ship_ownership (player_id, ship_id, is_primary, role_id) VALUES (v_player_id, v_ship_id, 1, 1);
    
    -- Link to Corp
    INSERT INTO corp_members (corp_id, player_id, role) VALUES (v_corp_id, v_player_id, 'Member');
  END LOOP;
END;
$$;

-- -----------------------------------------------------------------------------
-- 3) Orion Syndicate Setup
-- -----------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION setup_orion_syndicate()
RETURNS void LANGUAGE plpgsql AS $$
DECLARE
  v_sector int;
  v_corp_id int;
  v_player_id int;
  v_ship_id int;
  v_ship_type_id int;
BEGIN
  SELECT sector INTO v_sector FROM planets WHERE id = 3;
  SELECT id INTO v_corp_id FROM corporations WHERE tag = 'ORION';

  -- Zydras (Leader)
  INSERT INTO players (name, passwd, credits, sector, type, is_npc, loggedin, experience, alignment, commission)
  VALUES ('Zydras, Syndicate Leader', 'BOT', 10000, v_sector, 1, 1, 1, 500000, -10000, 32)
  RETURNING id INTO v_player_id;

  SELECT id INTO v_ship_type_id FROM shiptypes WHERE name = 'Orion Heavy Fighter Patrol';
  
  INSERT INTO ships (name, type_id, sector, fighters, shields, holds, perms)
  VALUES ('Orion Alpha One', v_ship_type_id, v_sector, 10000, 5000, 50, 777)
  RETURNING id INTO v_ship_id;

  UPDATE players SET ship = v_ship_id WHERE id = v_player_id;
  INSERT INTO ship_ownership (player_id, ship_id, is_primary, role_id) VALUES (v_player_id, v_ship_id, 1, 1);
  UPDATE corporations SET owner_id = v_player_id WHERE id = v_corp_id;
  INSERT INTO corp_members (corp_id, player_id, role) VALUES (v_corp_id, v_player_id, 'Leader');
END;
$$;

-- -----------------------------------------------------------------------------
-- 4) Fleet Spawning
-- -----------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION spawn_initial_fleet()
RETURNS void LANGUAGE plpgsql AS $$
DECLARE
  v_st_id int;
  v_shipname text;
  v_sector int;
  v_max_sectors int;
BEGIN
  SELECT MAX(id) INTO v_max_sectors FROM sectors;
  
  -- 1. Spawn the Imperial Flagship in a random high sector
  v_sector := 11 + floor(random() * (v_max_sectors - 10));
  SELECT id INTO v_st_id FROM shiptypes WHERE name = 'Imperial Starship (NPC)';
  
  INSERT INTO ships (name, type_id, sector, fighters, shields, holds, attack, photons, genesis, perms)
  VALUES ('ISS Emperor', v_st_id, v_sector, 32000, 65000, 100, 5000, 100, 10, 777);

  -- 2. Spawn 50 Derelicts
  FOR v_shipname IN SELECT name FROM npc_shipnames LOOP
    v_sector := 11 + floor(random() * (v_max_sectors - 10));
    SELECT id INTO v_st_id FROM shiptypes WHERE can_purchase = 1 ORDER BY random() LIMIT 1;
    
    INSERT INTO ships (name, type_id, sector, fighters, shields, holds, perms)
    VALUES (v_shipname, v_st_id, v_sector, 100, 100, 20, 777);
  END LOOP;
END;
$$;

COMMIT;
