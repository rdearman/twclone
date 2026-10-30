\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA bank_interest_122_test;
SET LOCAL search_path TO bank_interest_122_test, pg_catalog;
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
CREATE OR REPLACE FUNCTION apply_credit() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  UPDATE bank_accounts SET balance = balance + NEW.amount
   WHERE bank_accounts_id = NEW.account_id;
  RETURN NEW;
END $$;
CREATE TRIGGER bank_credit AFTER INSERT ON bank_transactions
FOR EACH ROW EXECUTE FUNCTION apply_credit();
\ir ../sql/pg/122_bank_interest_atomic.sql
SET search_path TO bank_interest_122_test, pg_catalog;
INSERT INTO bank_interest_policy VALUES (1, 365, 100, 1000000, NULL, 'daily', 'CRD');
INSERT INTO bank_accounts VALUES (1, 10000, 0, 'CRD', TRUE), (2, 50, 0, 'CRD', TRUE);
DO $test$
DECLARE r db_result;
BEGIN
  SELECT * INTO r FROM bank_apply_interest();
  IF r.code <> 0 OR r.id <> 1 OR (SELECT balance FROM bank_accounts WHERE bank_accounts_id=1) <> 10100 THEN
    RAISE EXCEPTION 'interest success failed: %', r;
  END IF;
  IF (SELECT balance FROM bank_accounts WHERE bank_accounts_id=2) <> 50 THEN
    RAISE EXCEPTION 'sub-minimum account changed';
  END IF;
  SELECT * INTO r FROM bank_apply_interest();
  IF r.code <> 0 OR r.id <> 0 OR (SELECT count(*) FROM bank_transactions) <> 1 THEN
    RAISE EXCEPTION 'same-day idempotency failed: %', r;
  END IF;
END
$test$;
-- A failing ledger insert must roll the entire procedure back, including policy state.
DELETE FROM bank_transactions;
ALTER TABLE bank_transactions ADD CONSTRAINT reject_interest CHECK (amount < 100);
UPDATE bank_interest_policy SET last_run_at = NULL;
UPDATE bank_accounts SET balance = 10000 WHERE bank_accounts_id = 1;
DO $test$
BEGIN
  BEGIN
    PERFORM bank_apply_interest();
    RAISE EXCEPTION 'expected rollback failure';
  EXCEPTION WHEN check_violation THEN NULL;
  END;
  IF (SELECT balance FROM bank_accounts WHERE bank_accounts_id=1) <> 10000
     OR (SELECT last_run_at FROM bank_interest_policy WHERE bank_interest_policy_id=1) IS NOT NULL THEN
    RAISE EXCEPTION 'rollback leaked state';
  END IF;
END
$test$;
ROLLBACK;
\echo 'PostgreSQL bank interest atomicity checks passed.'
