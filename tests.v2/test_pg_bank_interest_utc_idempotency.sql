\set ON_ERROR_STOP on

-- V3-ECO-02: the daily watermark and ledger group must share UTC semantics.
BEGIN;
CREATE SCHEMA bank_interest_utc_141_test;
SET LOCAL search_path TO bank_interest_utc_141_test, pg_catalog;

CREATE TYPE db_result AS (code integer, message text, id bigint);
CREATE TABLE currencies (code text PRIMARY KEY);
INSERT INTO currencies VALUES ('CRD');
CREATE TABLE bank_interest_policy (
  bank_interest_policy_id integer PRIMARY KEY CHECK (bank_interest_policy_id = 1),
  apr_bps integer NOT NULL CHECK (apr_bps >= 0),
  min_balance bigint NOT NULL DEFAULT 0,
  max_balance bigint NOT NULL DEFAULT 9223372036854775807,
  last_run_at timestamptz,
  compounding text NOT NULL DEFAULT 'none',
  currency text NOT NULL REFERENCES currencies(code)
);
CREATE TABLE bank_accounts (
  bank_accounts_id integer PRIMARY KEY,
  balance bigint NOT NULL CHECK (balance >= 0),
  interest_rate_bp integer NOT NULL DEFAULT 0,
  currency text NOT NULL REFERENCES currencies(code),
  is_active boolean NOT NULL DEFAULT TRUE
);
CREATE TABLE bank_transactions (
  bank_transactions_id serial PRIMARY KEY,
  account_id integer NOT NULL REFERENCES bank_accounts(bank_accounts_id),
  tx_type text NOT NULL,
  direction text NOT NULL,
  amount bigint NOT NULL CHECK (amount > 0),
  currency text NOT NULL,
  tx_group_id text,
  description text,
  ts bigint NOT NULL
);

CREATE UNIQUE INDEX ux_interest_utc_group
  ON bank_transactions (account_id, tx_group_id)
  WHERE tx_type = 'INTEREST' AND tx_group_id IS NOT NULL;

CREATE OR REPLACE FUNCTION settle_credit() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  UPDATE bank_accounts SET balance = balance + NEW.amount
   WHERE bank_accounts_id = NEW.account_id;
  RETURN NEW;
END $$;
CREATE TRIGGER bank_credit AFTER INSERT ON bank_transactions
FOR EACH ROW EXECUTE FUNCTION settle_credit();

\ir ../sql/pg/141_bank_interest_utc_idempotency.sql
SET search_path TO bank_interest_utc_141_test, pg_catalog;

INSERT INTO bank_interest_policy
  VALUES (1, 365, 100, 1000000, NULL, 'none', 'CRD');
INSERT INTO bank_accounts VALUES (1, 10000, 0, 'CRD', TRUE);

DO $test$
DECLARE
  r db_result;
  snapshot record;
  utc_day date := (CURRENT_TIMESTAMP AT TIME ZONE 'UTC')::date;
  utc_watermark timestamptz := (utc_day::text || ' 23:30:00+00')::timestamptz;
BEGIN
  SET LOCAL TIME ZONE 'Pacific/Kiritimati';

  SELECT * INTO r FROM bank_apply_interest();
  IF r.code <> 0 OR r.id <> 1
     OR (SELECT balance FROM bank_accounts WHERE bank_accounts_id = 1) <> 10100 THEN
    RAISE EXCEPTION 'initial UTC interest application failed: %', r;
  END IF;
  SELECT * INTO snapshot FROM bank_interest_utc_invariant_snapshot;
  IF snapshot.malformed_utc_groups <> 0
     OR snapshot.utc_group_timestamp_mismatches <> 0 THEN
    RAISE EXCEPTION 'UTC interest snapshot reported drift: %', snapshot;
  END IF;

  UPDATE bank_interest_policy SET last_run_at = utc_watermark;

  SELECT * INTO r FROM bank_apply_interest();
  IF r.code <> 0 OR r.id <> 0
     OR (SELECT count(*) FROM bank_transactions) <> 1
     OR (SELECT balance FROM bank_accounts WHERE bank_accounts_id = 1) <> 10100 THEN
    RAISE EXCEPTION 'same UTC-day interest replay was not refused: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL UTC bank-interest idempotency checks passed.'
