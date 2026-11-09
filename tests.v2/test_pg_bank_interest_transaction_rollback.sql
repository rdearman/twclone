\set ON_ERROR_STOP on

-- V3-ECO-02: interest accrual must be one transaction.  A ledger failure on a
-- later account must roll back earlier credits and the daily watermark.
BEGIN;
CREATE SCHEMA bank_interest_rollback_eco02_test;
SET LOCAL search_path TO bank_interest_rollback_eco02_test, pg_catalog;

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

CREATE OR REPLACE FUNCTION settle_credit() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF NEW.account_id = 2 THEN
    RAISE EXCEPTION 'simulated ledger write failure for account 2';
  END IF;
  UPDATE bank_accounts
     SET balance = balance + NEW.amount
   WHERE bank_accounts_id = NEW.account_id;
  RETURN NEW;
END $$;
CREATE TRIGGER bank_credit AFTER INSERT ON bank_transactions
FOR EACH ROW EXECUTE FUNCTION settle_credit();

\ir ../sql/pg/141_bank_interest_utc_idempotency.sql
\ir ../sql/pg/129_bank_interest_ledger_invariants.sql
SET search_path TO bank_interest_rollback_eco02_test, pg_catalog;

INSERT INTO bank_interest_policy
  VALUES (1, 365, 100, 1000000, NULL, 'none', 'CRD');
INSERT INTO bank_accounts VALUES
  (1, 10000, 0, 'CRD', TRUE),
  (2, 10000, 0, 'CRD', TRUE);

DO $test$
DECLARE
  r db_result;
  failed boolean := false;
  snapshot record;
BEGIN
  BEGIN
    SELECT * INTO r FROM bank_apply_interest();
  EXCEPTION WHEN OTHERS THEN
    failed := true;
  END;

  IF NOT failed THEN
    RAISE EXCEPTION 'interest ledger failure was unexpectedly swallowed';
  END IF;
  IF (SELECT count(*) FROM bank_transactions) <> 0
     OR (SELECT balance FROM bank_accounts WHERE bank_accounts_id = 1) <> 10000
     OR (SELECT balance FROM bank_accounts WHERE bank_accounts_id = 2) <> 10000
     OR (SELECT last_run_at FROM bank_interest_policy WHERE bank_interest_policy_id = 1)
        IS NOT NULL THEN
    RAISE EXCEPTION 'interest failure left partial account or watermark state';
  END IF;

  SELECT * INTO snapshot FROM bank_interest_ledger_invariant_snapshot;
  IF snapshot.malformed_interest_transactions <> 0
     OR snapshot.interest_currency_mismatches <> 0
     OR snapshot.duplicate_interest_groups <> 0 THEN
    RAISE EXCEPTION 'rollback left interest ledger invariant drift: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL bank-interest transaction rollback checks passed.'
