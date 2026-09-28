-- Run after Big Bang generates ports in a disposable PostgreSQL database.
-- COL is valid ship cargo but is not in the port_trade commodity contract.
DO $test$
DECLARE
  v_tradable_count integer;
BEGIN
  SELECT count(*) INTO v_tradable_count
    FROM commodities
   WHERE code IN ('ORE', 'ORG', 'EQU', 'SLV', 'WPN', 'DRG');

  IF NOT EXISTS (SELECT 1 FROM ports WHERE type = 10) THEN
    RAISE EXCEPTION 'fixture must contain a generated black market port';
  END IF;

  IF EXISTS (SELECT 1 FROM port_trade WHERE commodity = 'COL') THEN
    RAISE EXCEPTION 'COL ship cargo must not be inserted into port_trade';
  END IF;

  IF EXISTS (
     SELECT 1
      FROM ports p
     WHERE p.type = 10
       AND EXISTS (SELECT 1 FROM port_trade pt WHERE pt.port_id = p.port_id)
       AND (SELECT count(*)
              FROM port_trade pt
             WHERE pt.port_id = p.port_id) <> v_tradable_count * 2
  ) THEN
    RAISE EXCEPTION 'black market ports must buy and sell every tradable commodity';
  END IF;
END
$test$;

\echo 'PostgreSQL port generator passed: black markets trade catalog goods except internal COL cargo.'
