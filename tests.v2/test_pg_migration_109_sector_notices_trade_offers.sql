-- Isolated PostgreSQL test for migration 109. All schema objects are temporary.
SET search_path TO pg_temp, public;

CREATE TEMP TABLE sectors (sector_id integer PRIMARY KEY);
CREATE TEMP TABLE players (player_id integer PRIMARY KEY);
CREATE TEMP TABLE commodities (code text PRIMARY KEY);
CREATE TEMP TABLE system_notice (
  system_notice_id bigserial PRIMARY KEY,
  created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
  title text NOT NULL,
  body text NOT NULL,
  severity text NOT NULL CHECK (severity IN ('info', 'warn', 'error')),
  expires_at timestamptz
);

INSERT INTO sectors VALUES (7);
INSERT INTO players VALUES (11), (12);
INSERT INTO commodities VALUES ('ORE');
INSERT INTO system_notice (title, body, severity)
VALUES ('Legacy notice', 'Still global', 'info');

\ir ../sql/pg/109_v2_sector_notices_trade_offers.sql
\ir ../sql/pg/109_v2_sector_notices_trade_offers.sql

INSERT INTO system_notice
  (title, body, severity, scope, sector_id, meta, expires_at, notice_key)
VALUES
  ('Sector event', 'Beacon deployed', 'info', 'sector', 7,
   '{"subtype":"beacon_set"}'::jsonb, CURRENT_TIMESTAMP + INTERVAL '1 hour', 'beacon:7:42');

INSERT INTO system_notice (title, body, severity, scope, player_id)
VALUES ('Player notice', 'Existing publisher contract', 'info', 'player', 11);

INSERT INTO trade_offers
  (sender_player_id, recipient_player_id, commodity_code, mode, quantity,
   unit_price, expires_at, idempotency_key)
VALUES
  (11, 12, 'ORE', 'sell', 3, 250, CURRENT_TIMESTAMP + INTERVAL '1 hour', 'request-1');

DO $test$
BEGIN
  IF (SELECT scope FROM system_notice WHERE system_notice_id = 1) <> 'global' THEN
    RAISE EXCEPTION 'legacy notices must remain global';
  END IF;

  IF (SELECT sector_id FROM system_notice WHERE notice_key = 'beacon:7:42') <> 7 THEN
    RAISE EXCEPTION 'sector notice scope was not persisted';
  END IF;

  IF (SELECT count(*) FROM pg_constraint
      WHERE conrelid = 'trade_offers'::regclass AND contype = 'f') <> 3 THEN
    RAISE EXCEPTION 'trade offer foreign keys are missing';
  END IF;

  IF NOT EXISTS (SELECT 1 FROM pg_indexes
      WHERE schemaname = current_schema()
        AND tablename = 'trade_offers'
        AND indexname = 'idx_trade_offers_pending_expiry') THEN
    RAISE EXCEPTION 'trade offer expiry index is missing';
  END IF;

  IF NOT EXISTS (SELECT 1 FROM pg_indexes
      WHERE schemaname = current_schema()
        AND tablename = 'system_notice'
        AND indexname = 'ux_system_notice_notice_key') THEN
    RAISE EXCEPTION 'notice idempotency index is missing';
  END IF;
END
$test$;

DO $constraints$
BEGIN
  BEGIN
    INSERT INTO system_notice (title, body, severity, scope)
    VALUES ('Invalid sector notice', 'Missing sector', 'info', 'sector');
    RAISE EXCEPTION 'sector notice without sector_id unexpectedly succeeded';
  EXCEPTION WHEN check_violation THEN
    NULL;
  END;

  BEGIN
    INSERT INTO trade_offers
      (sender_player_id, recipient_player_id, commodity_code, mode,
       quantity, unit_price, expires_at, idempotency_key)
    VALUES
      (11, 12, 'ORE', 'sell', 1, 10, CURRENT_TIMESTAMP + INTERVAL '1 hour', 'request-1');
    RAISE EXCEPTION 'duplicate offer idempotency key unexpectedly succeeded';
  EXCEPTION WHEN unique_violation THEN
    NULL;
  END;
END
$constraints$;

\echo 'PostgreSQL migration 109 passed: legacy notices, sector scope, offer lifecycle schema, expiry, and idempotency.'
