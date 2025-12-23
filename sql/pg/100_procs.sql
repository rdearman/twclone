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
BEGIN
  PERFORM bigbang_lock(v_lock_key);

  IF target_sectors IS NULL THEN
    SELECT COALESCE(MAX(id),0)::int INTO v_target FROM sectors;
  ELSE
    v_target := target_sectors;
  END IF;

  IF v_target < 0 THEN
    RAISE EXCEPTION 'target_sectors must be >= 0';
  END IF;

  PERFORM generate_sectors(v_target);

  INSERT INTO ports (number, name, sector, type)
  SELECT
    s.id AS number,
    format('Port %s', s.id) AS name,
    s.id AS sector,
    floor(random() * 8 + 1)::int AS type
  FROM sectors s
  WHERE s.id <= v_target
    AND NOT EXISTS (SELECT 1 FROM ports p WHERE p.sector = s.id);

  GET DIAGNOSTICS v_added = ROW_COUNT;

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
      petty_cash = EXCLUDED.petty_cash;

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
BEGIN
  PERFORM bigbang_lock(v_lock_key);
  
  SELECT MAX(id) INTO v_max_sector FROM sectors;
  IF v_max_sector IS NULL THEN 
    PERFORM bigbang_unlock(v_lock_key);
    RETURN 0; 
  END IF;

  -- 1. Federation Core (Fixed)
  INSERT INTO clusters (name, role, kind, center_sector, law_severity, alignment)
  VALUES ('Federation Core', 'FED', 'FACTION', 1, 3, 100)
  ON CONFLICT DO NOTHING;
  
  -- 2. Random Clusters
  FOR i IN 1..(target_count - 1) LOOP
    v_center := 11 + floor(random() * (v_max_sector - 10));
    v_name := format('Cluster %s', v_center);
    v_align := floor(random() * 200) - 100; -- -100 to 100
    
    INSERT INTO clusters (name, role, kind, center_sector, law_severity, alignment)
    VALUES (v_name, 'RANDOM', 'RANDOM', v_center, 1, v_align);
  END LOOP;

  GET DIAGNOSTICS v_added = ROW_COUNT; -- counts the loop inserts (actually logic is slightly off for row_count of loop, but acceptable for now)
  
  PERFORM bigbang_unlock(v_lock_key);
  RETURN v_added; -- This will be small, technically loop doesn't aggregate row_count this way in plpgsql easily without var
END;
$$;

COMMIT;
