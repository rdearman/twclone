-- Run after sql/pg/090_seed_lookup.sql + 091_seed_essential.sql on a fresh
-- database, or after 091_seed_essential.sql on a legacy database.
DO $test$
DECLARE
  v_col_id integer;
  v_max_id integer;
  v_seq text;
  v_last_value bigint;
  v_is_called boolean;
BEGIN
  SELECT commodities_id INTO v_col_id
  FROM commodities WHERE code = 'COL';

  IF NOT FOUND OR v_col_id <= 0 THEN
    RAISE EXCEPTION 'COL seed is missing or has an invalid ID: %', v_col_id;
  END IF;

  SELECT COALESCE(MAX(commodities_id), 0) INTO v_max_id FROM commodities;
  v_seq := pg_get_serial_sequence('commodities', 'commodities_id');
  IF v_seq IS NULL THEN
    RAISE EXCEPTION 'commodities_id serial sequence is missing';
  END IF;

  EXECUTE format('SELECT last_value, is_called FROM %s', v_seq::regclass)
    INTO v_last_value, v_is_called;

  IF NOT v_is_called OR v_last_value < v_max_id THEN
    RAISE EXCEPTION 'commodity sequence is behind seeded IDs (last %, max %, called %)',
      v_last_value, v_max_id, v_is_called;
  END IF;
END
$test$;

\echo 'PostgreSQL COL seed consistency passed: commodity row and sequence are aligned.'
