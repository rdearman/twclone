-- Isolated migration test: temporary relations shadow any public tables.
SET search_path TO pg_temp, public;

CREATE TEMP TABLE ports (
  port_id integer PRIMARY KEY,
  type integer NOT NULL,
  porttype_id integer
);

INSERT INTO ports (port_id, type) VALUES
  (1, 0), (2, 9), (3, 10), (4, 1);

\ir ../sql/pg/102_phase5_porttypes.sql
\ir ../sql/pg/102_phase5_porttypes.sql

DO $test$
DECLARE
  mapped_class0 integer;
  mapped_stardock integer;
  mapped_blackmarket integer;
  mapped_other_standard integer;
  fk_count integer;
BEGIN
  SELECT pt.porttype_id INTO mapped_class0 FROM porttypes pt WHERE pt.code = 'CLASS0';
  SELECT pt.porttype_id INTO mapped_stardock FROM porttypes pt WHERE pt.code = 'STARDOCK';
  SELECT pt.porttype_id INTO mapped_blackmarket FROM porttypes pt WHERE pt.code = 'BLACKMARKET';

  SELECT porttype_id INTO mapped_other_standard FROM ports WHERE port_id = 4;
  IF (SELECT porttype_id FROM ports WHERE port_id = 1) <> mapped_class0
     OR (SELECT porttype_id FROM ports WHERE port_id = 2) <> mapped_stardock
     OR (SELECT porttype_id FROM ports WHERE port_id = 3) <> mapped_blackmarket
     OR mapped_other_standard <> mapped_class0 THEN
    RAISE EXCEPTION 'port type backfill did not preserve expected mappings';
  END IF;

  SELECT count(*) INTO fk_count
  FROM pg_constraint
  WHERE conrelid = 'ports'::regclass AND conname = 'fk_ports_porttype';
  IF fk_count <> 1 THEN
    RAISE EXCEPTION 'expected one fk_ports_porttype constraint, found %', fk_count;
  END IF;

  IF NOT EXISTS (
    SELECT 1 FROM pg_indexes
     WHERE schemaname = current_schema()
       AND tablename = 'ports'
       AND indexname = 'idx_ports_porttype_id'
  ) THEN
    RAISE EXCEPTION 'porttype lookup index is missing';
  END IF;
END
$test$;

\echo 'Migration 102 passed: mappings preserved and rerun safe.'
