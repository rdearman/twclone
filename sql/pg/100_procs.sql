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
    CASE 
      WHEN gs > 10 AND random() < 0.6 THEN constellation_name()
      ELSE NULL 
    END AS name,
    NULL::text AS beacon,
    NULL::text AS nebulae
  FROM generate_series(v_max_id + 1, target_count) AS gs;

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

  INSERT INTO ports (number, name, sector)
  SELECT
    s.id AS number,
    format('Port %s', s.id) AS name,
    s.id AS sector
  FROM sectors s
  WHERE s.id <= v_target
    AND NOT EXISTS (SELECT 1 FROM ports p WHERE p.sector = s.id);

  GET DIAGNOSTICS v_added = ROW_COUNT;

  PERFORM bigbang_unlock(v_lock_key);
  RETURN v_added;
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

COMMIT;
