-- 100_procs.sql
-- BigBang stored procedures/functions for Postgres.
-- Fixed: GET DIAGNOSTICS syntax and timestamp data types.

BEGIN;

-- ---------------------------------------------------------------------------
-- Advisory lock helpers (prevents two bigbang runs colliding)
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION bigbang_lock(p_key bigint)
RETURNS void LANGUAGE plpgsql AS $$
BEGIN
  PERFORM pg_advisory_lock(p_key);
END;
$$;

CREATE OR REPLACE FUNCTION bigbang_unlock(p_key bigint)
RETURNS void LANGUAGE plpgsql AS $$
BEGIN
  PERFORM pg_advisory_unlock(p_key);
END;
$$;

-- ---------------------------------------------------------------------------
-- 1) Sectors
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_sectors(target_count int)
RETURNS bigint
LANGUAGE plpgsql
AS $$
DECLARE
  v_max_id bigint;
  v_added  bigint := 0;
  v_lock_key bigint := 72002001;
  v_beacons text[] := ARRAY[
    'For a good time, warp to Sector 69.',
    'The Federation is lying to you regarding Ore prices!',
    'Don''t trust the auto-pilot...',
    'Red Dragon Corp rules this void!',
    'Will trade sister for a Genesis Torpedo.',
    'I hid 500,000 credits in Sector... [text unreadable]',
    'The Feds is watching you.',
    'Mining is for droids. Real pilots steal.',
    'If you can read this, you''re in range of my cannons.',
    'Turn back. Here there be dragons.'
  ];
BEGIN
  IF target_count IS NULL OR target_count < 0 THEN
    RAISE EXCEPTION 'target_count must be >= 0';
  END IF;

  PERFORM bigbang_lock(v_lock_key);

  SELECT COALESCE(MAX(id), 0) INTO v_max_id FROM sectors;

  IF v_max_id >= target_count THEN
    PERFORM bigbang_unlock(v_lock_key);
    RETURN 0;
  END IF;

  INSERT INTO sectors (id, name, beacon, nebulae)
  SELECT
    gs AS id,
    sub.name_val AS name,
    CASE 
      WHEN random() < 0.02 THEN v_beacons[1 + floor(random() * array_length(v_beacons, 1))::int]
      ELSE NULL 
    END AS beacon,
    sub.name_val AS nebulae
  FROM generate_series(v_max_id + 1, target_count) AS gs,
  LATERAL (
    SELECT 
      CASE 
        WHEN gs > 10 AND random() < 0.6 THEN constellation_name()
        ELSE NULL 
      END AS name_val
  ) sub;

  GET DIAGNOSTICS v_added = ROW_COUNT;

  PERFORM bigbang_unlock(v_lock_key);
  RETURN v_added;
END;
$$;

-- ---------------------------------------------------------------------------
-- 2) Ports
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_ports(target_sectors int DEFAULT NULL)
RETURNS bigint
LANGUAGE plpgsql
AS $$
DECLARE
  v_added bigint := 0;
  v_lock_key bigint := 72002002;
  v_target int;
  v_port_ratio int;
  v_port_rec record;
  v_commodity_id int;
  v_base_price int;
  v_code text;
  v_cluster_alignment int;
BEGIN
  PERFORM bigbang_lock(v_lock_key);

  -- Get config value for port_ratio, default to 40 if not set
  SELECT value::int INTO v_port_ratio FROM config WHERE key = 'port_ratio';
  IF v_port_ratio IS NULL OR v_port_ratio > 100 OR v_port_ratio < 0 THEN
    v_port_ratio := 40;
  END IF;

  IF target_sectors IS NULL THEN
    SELECT COALESCE(MAX(id),0)::int INTO v_target FROM sectors;
  ELSE
    v_target := target_sectors;
  END IF;

  IF v_target < 0 THEN
    RAISE EXCEPTION 'target_sectors must be >= 0';
  END IF;

  PERFORM generate_sectors(v_target);

  -- Create a temporary table to hold newly created port IDs and types
  CREATE TEMP TABLE new_ports (id int, type int, sector int) ON COMMIT DROP;

  -- Insert ports respecting the port_ratio, and capture their IDs and types
  WITH inserted_ports AS (
    INSERT INTO ports (number, name, sector, type)
    SELECT
      s.id AS number,
      randomname() AS name,
      s.id AS sector,
      floor(random() * 8 + 1)::int AS type
    FROM sectors s
    WHERE s.id > 10 -- Don't create ports in Fedspace core
      AND s.id <= v_target
      AND NOT EXISTS (SELECT 1 FROM ports p WHERE p.sector = s.id)
      AND random() <= (v_port_ratio / 100.0)
    RETURNING id, type, sector
  )
  INSERT INTO new_ports SELECT id, type, sector FROM inserted_ports;
  
  GET DIAGNOSTICS v_added = ROW_COUNT;

  -- Now, seed commodities for the new ports
  FOR v_port_rec IN SELECT id, type, sector FROM new_ports LOOP
    -- SBB (Sell Ore, Buy Organics/Equipment)
    IF v_port_rec.type = 1 THEN 
      INSERT INTO port_trade (port_id, commodity, mode) VALUES (v_port_rec.id, 'organics', 'buy'), (v_port_rec.id, 'equipment', 'buy'), (v_port_rec.id, 'ore', 'sell');
    -- SSB
    ELSIF v_port_rec.type = 2 THEN 
      INSERT INTO port_trade (port_id, commodity, mode) VALUES (v_port_rec.id, 'ore', 'sell'), (v_port_rec.id, 'organics', 'sell'), (v_port_rec.id, 'equipment', 'buy');
    -- BSS
    ELSIF v_port_rec.type = 3 THEN
      INSERT INTO port_trade (port_id, commodity, mode) VALUES (v_port_rec.id, 'ore', 'buy'), (v_port_rec.id, 'organics', 'sell'), (v_port_rec.id, 'equipment', 'sell');
    -- BSB
    ELSIF v_port_rec.type = 4 THEN
      INSERT INTO port_trade (port_id, commodity, mode) VALUES (v_port_rec.id, 'ore', 'buy'), (v_port_rec.id, 'organics', 'sell'), (v_port_rec.id, 'equipment', 'buy');
    -- BBS
    ELSIF v_port_rec.type = 5 THEN
      INSERT INTO port_trade (port_id, commodity, mode) VALUES (v_port_rec.id, 'ore', 'buy'), (v_port_rec.id, 'organics', 'buy'), (v_port_rec.id, 'equipment', 'sell');
    -- SBS
    ELSIF v_port_rec.type = 6 THEN
      INSERT INTO port_trade (port_id, commodity, mode) VALUES (v_port_rec.id, 'ore', 'sell'), (v_port_rec.id, 'organics', 'buy'), (v_port_rec.id, 'equipment', 'sell');
    -- BBB (Specialty Port)
    ELSIF v_port_rec.type = 7 THEN
      INSERT INTO port_trade (port_id, commodity, mode) VALUES (v_port_rec.id, 'ore', 'buy'), (v_port_rec.id, 'organics', 'buy'), (v_port_rec.id, 'equipment', 'buy');
    -- SSS (Specialty Port)
    ELSIF v_port_rec.type = 8 THEN
      INSERT INTO port_trade (port_id, commodity, mode) VALUES (v_port_rec.id, 'ore', 'sell'), (v_port_rec.id, 'organics', 'sell'), (v_port_rec.id, 'equipment', 'sell');
    END IF;

    -- Seed initial stock for all commodities at the new port
    FOR v_commodity_id, v_code, v_base_price IN SELECT id, code, base_price FROM commodities WHERE illegal = 0 LOOP
        INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price)
        VALUES ('port', v_port_rec.id, v_code, 10000, v_base_price)
        ON CONFLICT DO NOTHING;
    END LOOP;

    -- Check cluster alignment for illegal goods
    v_cluster_alignment := 0; -- Default to neutral
    SELECT cl.alignment INTO v_cluster_alignment
    FROM clusters cl JOIN cluster_sectors cs ON cs.cluster_id = cl.id
    WHERE cs.sector_id = v_port_rec.sector
    LIMIT 1;

    IF v_cluster_alignment < 0 THEN
        -- Seed illegal goods for evil clusters
        FOR v_commodity_id, v_code, v_base_price IN SELECT id, code, base_price FROM commodities WHERE illegal = 1 LOOP
            INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price)
            VALUES ('port', v_port_rec.id, v_code, 500, v_base_price)
            ON CONFLICT DO NOTHING;
        END LOOP;
    END IF;

  END LOOP;

  PERFORM bigbang_unlock(v_lock_key);
  RETURN v_added;
END;
$$;

-- ---------------------------------------------------------------------------
-- 2.5) Stardock
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_stardock()
RETURNS bigint
LANGUAGE plpgsql
AS $$
DECLARE
  v_sector int;
  v_max_sector int;
  v_lock_key bigint := 72002006;
  v_exists boolean;
  v_port_id int;
BEGIN
  PERFORM bigbang_lock(v_lock_key);

  -- Check if Stardock exists
  SELECT EXISTS(SELECT 1 FROM ports WHERE type = 9) INTO v_exists;
  IF v_exists THEN
    PERFORM bigbang_unlock(v_lock_key);
    RETURN 0;
  END IF;

  SELECT MAX(id) INTO v_max_sector FROM sectors;
  -- Pick random sector > 10
  v_sector := 11 + floor(random() * (v_max_sector - 10));

  -- Insert Stardock (Type 9, Size 10, Tech 10)
  INSERT INTO ports (number, name, sector, type, size, techlevel, petty_cash)
  VALUES (v_sector, 'Stardock', v_sector, 9, 10, 10, 1000000)
  ON CONFLICT (sector, number) DO UPDATE 
  SET name = EXCLUDED.name, 
      type = EXCLUDED.type, 
      size = EXCLUDED.size, 
      techlevel = EXCLUDED.techlevel, 
      petty_cash = EXCLUDED.petty_cash
  RETURNING id INTO v_port_id;

  -- Seed Stardock Assets
  IF v_port_id IS NOT NULL THEN
    INSERT INTO stardock_assets (sector_id, owner_id, fighters, defenses, ship_capacity)
    VALUES (v_sector, 1, 10000, 500, 100) -- Assuming owner_id 1 is System/Fed
    ON CONFLICT (sector_id) DO NOTHING;

    -- Seed Shipyard with all purchasable ships
    INSERT INTO shipyard_inventory (port_id, ship_type_id, enabled)
    SELECT v_port_id, id, 1
    FROM shiptypes
    WHERE can_purchase = TRUE
    ON CONFLICT (port_id, ship_type_id) DO NOTHING;
  END IF;

  PERFORM bigbang_unlock(v_lock_key);
  RETURN 1;
END;
$$;

-- ---------------------------------------------------------------------------
-- 3) Planets
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_planets(target_count int)
RETURNS bigint
LANGUAGE plpgsql
AS $$
DECLARE
  v_existing bigint;
  v_to_add   bigint;
  v_added    bigint := 0;
  v_lock_key bigint := 72002003;
  v_sector_count bigint;
  v_base_num bigint;
  v_ptype bigint;
BEGIN
  IF target_count IS NULL OR target_count < 0 THEN
    RAISE EXCEPTION 'target_count must be >= 0';
  END IF;

  PERFORM bigbang_lock(v_lock_key);

  SELECT COUNT(*) INTO v_existing FROM planets;
  v_to_add := GREATEST(target_count - v_existing, 0);

  IF v_to_add = 0 THEN
    PERFORM bigbang_unlock(v_lock_key);
    RETURN 0;
  END IF;

  SELECT COUNT(*) INTO v_sector_count FROM sectors;
  IF v_sector_count = 0 THEN
    RAISE EXCEPTION 'Cannot generate planets: no sectors exist';
  END IF;

  SELECT COALESCE(MAX(num), 0) + 1 INTO v_base_num FROM planets;

  SELECT id INTO v_ptype FROM planettypes ORDER BY id LIMIT 1;
  IF v_ptype IS NULL THEN
    RAISE EXCEPTION 'Cannot generate planets: no planettypes exist';
  END IF;

  -- Fixed: created_at now inserts a timestamp (now()), not a BigInt epoch
  INSERT INTO planets (
    num, sector, name,
    owner_id, owner_type, class,
    type,
    created_at, created_by
  )
  SELECT
    (v_base_num + gs - 1) AS num,
    ((gs - 1) % v_sector_count + 1)::int AS sector,
    format('Planet %s', (v_base_num + gs - 1)) AS name,
    0 AS owner_id,
    'none' AS owner_type,
    'M' AS class,
    v_ptype AS type,
    now() AS created_at,
    0 AS created_by
  FROM generate_series(1, v_to_add) AS gs;

  GET DIAGNOSTICS v_added = ROW_COUNT;

  PERFORM bigbang_unlock(v_lock_key);
  RETURN v_added;
END;
$$;

-- ---------------------------------------------------------------------------
-- 4) seed_factions()
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION seed_factions()
RETURNS bigint
LANGUAGE plpgsql
AS $$
DECLARE
  v_added bigint := 0;
  v_rows  bigint; -- Temp variable to capture ROW_COUNT
  v_lock_key bigint := 72002004;
BEGIN
  PERFORM bigbang_lock(v_lock_key);

  INSERT INTO corporations (name, owner_id, tag, description)
  SELECT 'Imperial', NULL, 'IMP', 'Imperial faction'
  WHERE NOT EXISTS (SELECT 1 FROM corporations WHERE lower(name) = lower('Imperial'));
  
  -- Corrected: Capture row count first, then add to total
  GET DIAGNOSTICS v_rows = ROW_COUNT;
  v_added := v_added + v_rows;

  INSERT INTO corporations (name, owner_id, tag, description)
  SELECT 'Ferringhi', NULL, 'FER', 'Ferringhi faction'
  WHERE NOT EXISTS (SELECT 1 FROM corporations WHERE lower(name) = lower('Ferringhi'));
  
  -- Corrected: Capture row count first, then add to total
  GET DIAGNOSTICS v_rows = ROW_COUNT;
  v_added := v_added + v_rows;

  PERFORM bigbang_unlock(v_lock_key);
  RETURN v_added;
END;
$$;

-- ---------------------------------------------------------------------------
-- 5) Clusters
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_clusters(target_count int DEFAULT 10)
RETURNS bigint
LANGUAGE plpgsql
AS $$
DECLARE
  v_added bigint := 0;
  v_lock_key bigint := 72002005;
  v_max_sector bigint;
  v_center int;
  v_name text;
  v_align int;
  v_cluster_id int;
BEGIN
  PERFORM bigbang_lock(v_lock_key);
  
  -- Truncate for idempotency
  TRUNCATE cluster_sectors;

  SELECT MAX(id) INTO v_max_sector FROM sectors;
  IF v_max_sector IS NULL THEN 
    PERFORM bigbang_unlock(v_lock_key);
    RETURN 0; 
  END IF;

  -- 1. Federation Core (Fixed)
  INSERT INTO clusters (name, role, kind, center_sector, law_severity, alignment)
  VALUES ('Federation Core', 'FED', 'FACTION', 1, 3, 100)
  ON CONFLICT (name) DO UPDATE SET role = EXCLUDED.role, kind = EXCLUDED.kind, center_sector = EXCLUDED.center_sector, law_severity = EXCLUDED.law_severity, alignment = EXCLUDED.alignment;
  
  -- 2. Random Clusters
  FOR i IN 1..(target_count - 1) LOOP
    v_center := 11 + floor(random() * (v_max_sector - 10));
    v_name := format('Cluster %s', v_center);
    v_align := floor(random() * 200) - 100; -- -100 to 100
    
    INSERT INTO clusters (name, role, kind, center_sector, law_severity, alignment)
    VALUES (v_name, 'RANDOM', 'RANDOM', v_center, 1, v_align)
    ON CONFLICT (name) DO UPDATE SET role = EXCLUDED.role, kind = EXCLUDED.kind, center_sector = EXCLUDED.center_sector, law_severity = EXCLUDED.law_severity, alignment = EXCLUDED.alignment;
  END LOOP;
  
  -- 3. Populate cluster_sectors table
  FOR v_cluster_id, v_center IN SELECT id, center_sector FROM clusters LOOP
      -- Add center sector
      INSERT INTO cluster_sectors (cluster_id, sector_id) VALUES (v_cluster_id, v_center) ON CONFLICT DO NOTHING;
      -- Add adjacent sectors
      INSERT INTO cluster_sectors (cluster_id, sector_id)
      SELECT v_cluster_id, to_sector FROM sector_warps WHERE from_sector = v_center
      ON CONFLICT DO NOTHING;
  END LOOP;

  GET DIAGNOSTICS v_added = ROW_COUNT;
  
  PERFORM bigbang_unlock(v_lock_key);
  RETURN v_added;
END;
$$;


-- ---------------------------------------------------------------------------
-- 6) Taverns
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_taverns(p_ratio_pct int DEFAULT 20)
RETURNS bigint
LANGUAGE plpgsql
AS $$
DECLARE
  v_added bigint := 0;
  v_lock_key bigint := 72002007;
  v_ratio numeric := p_ratio_pct / 100.0;
BEGIN
  PERFORM bigbang_lock(v_lock_key);

  INSERT INTO taverns (sector_id, name_id)
  SELECT 
    s.id,
    (SELECT id FROM tavern_names ORDER BY random() LIMIT 1)
  FROM sectors s
  WHERE s.id > 10 -- No taverns in Fedspace core
    AND NOT EXISTS (SELECT 1 FROM taverns t WHERE t.sector_id = s.id)
    AND random() < v_ratio;

  GET DIAGNOSTICS v_added = ROW_COUNT;

  PERFORM bigbang_unlock(v_lock_key);
  RETURN v_added;
END;
$$;

COMMIT;
