-- Run only after migration 110 in a disposable PostgreSQL test database.
-- Exercises persisted hazard typing, severity bounds, and sector FK semantics.
BEGIN;
\ir ../sql/pg/110_sector_environmental_hazards.sql
\ir ../sql/pg/110_sector_environmental_hazards.sql

DO $test$
DECLARE
  v_sector integer;
  v_count integer;
BEGIN
  IF NOT EXISTS (
      SELECT 1 FROM pg_constraint
       WHERE conrelid = 'sector_hazards'::regclass
         AND contype = 'f') THEN
    RAISE EXCEPTION 'sector_hazards foreign key is missing';
  END IF;
  IF NOT EXISTS (
      SELECT 1 FROM pg_indexes
       WHERE schemaname = current_schema()
         AND indexname = 'idx_sector_hazards_type') THEN
    RAISE EXCEPTION 'sector hazard type index is missing';
  END IF;

  SELECT MIN(sector_id) INTO v_sector FROM sectors;
  IF v_sector IS NULL THEN
    RAISE EXCEPTION 'fixture must contain at least one sector';
  END IF;

  INSERT INTO sector_hazards (sector_id, hazard_type, severity)
  VALUES (v_sector, 'volcanic', 17), (v_sector, 'nebula', 23), (v_sector, 'radiation', 31)
  ON CONFLICT (sector_id, hazard_type) DO UPDATE SET severity = EXCLUDED.severity;

  SELECT COUNT(*) INTO v_count FROM sector_hazards
   WHERE sector_id = v_sector AND hazard_type IN ('volcanic', 'nebula', 'radiation');
  IF v_count <> 3 THEN
    RAISE EXCEPTION 'expected all three supported hazard types, found %', v_count;
  END IF;

  UPDATE sector_hazards SET severity = 29
   WHERE sector_id = v_sector AND hazard_type = 'nebula';
  UPDATE sector_hazards SET severity = 23
   WHERE sector_id = v_sector AND hazard_type = 'nebula';
  IF (SELECT severity FROM sector_hazards
       WHERE sector_id = v_sector AND hazard_type = 'nebula') <> 23 THEN
    RAISE EXCEPTION 'hazard severity did not persist';
  END IF;

  BEGIN
    INSERT INTO sector_hazards (sector_id, hazard_type, severity)
    VALUES (v_sector, 'unknown', 1);
    RAISE EXCEPTION 'unsupported hazard type was accepted';
  EXCEPTION WHEN check_violation THEN
    NULL;
  END;

  BEGIN
    INSERT INTO sector_hazards (sector_id, hazard_type, severity)
    VALUES (v_sector, 'radiation', 0);
    RAISE EXCEPTION 'zero hazard severity was accepted';
  EXCEPTION WHEN check_violation THEN
    NULL;
  END;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL sector hazard persistence checks passed.'
