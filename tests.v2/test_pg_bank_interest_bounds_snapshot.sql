\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA bank_interest_bounds_eco02_test;
SET LOCAL search_path TO bank_interest_bounds_eco02_test, pg_catalog;

CREATE TYPE db_result AS (code integer, message text, id bigint);
CREATE TABLE currencies (code text PRIMARY KEY);
INSERT INTO currencies VALUES ('CRD');
CREATE TABLE bank_interest_policy (
  bank_interest_policy_id integer PRIMARY KEY CHECK (bank_interest_policy_id = 1),
  apr_bps integer NOT NULL CHECK (apr_bps >= 0),
  min_balance bigint NOT NULL DEFAULT 0,
  max_balance bigint NOT NULL,
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
\ir ../sql/pg/129_bank_interest_ledger_invariants.sql
SET search_path TO bank_interest_bounds_eco02_test, pg_catalog;

INSERT INTO bank_interest_policy
  VALUES (1, 365, 100, 1000000, NULL, 'none', 'CRD');
INSERT INTO bank_accounts (bank_accounts_id, balance, interest_rate_bp, currency, is_active)
VALUES
  (1, 999500, 0,   'CRD', TRUE),  -- policy APR would exceed the cap
  (2, 10000,  730, 'CRD', TRUE),  -- account rate overrides policy APR
  (3, 10000,  0,   'CRD', FALSE), -- inactive accounts are excluded
  (4, 50,     0,   'CRD', TRUE);  -- below the policy minimum

DO $test$
DECLARE
  r db_result;
  snapshot record;
BEGIN
  SELECT * INTO r FROM bank_apply_interest();
  IF r.code <> 0 OR r.id <> 2 THEN
    RAISE EXCEPTION 'expected two credited accounts, got: %', r;
  END IF;

  IF (SELECT balance FROM bank_accounts WHERE bank_accounts_id = 1) <> 1000000
     OR (SELECT balance FROM bank_accounts WHERE bank_accounts_id = 2) <> 10200
     OR (SELECT balance FROM bank_accounts WHERE bank_accounts_id = 3) <> 10000
     OR (SELECT balance FROM bank_accounts WHERE bank_accounts_id = 4) <> 50 THEN
    RAISE EXCEPTION 'interest bounds/account eligibility incorrect';
  END IF;

  SELECT * INTO snapshot FROM bank_interest_ledger_invariant_snapshot;
  IF snapshot.malformed_interest_transactions <> 0
     OR snapshot.interest_currency_mismatches <> 0
     OR snapshot.duplicate_interest_groups <> 0 THEN
    RAISE EXCEPTION 'interest ledger invariant violation: %', snapshot;
  END IF;

  SELECT * INTO r FROM bank_apply_interest();
  IF r.code <> 0 OR r.id <> 0
     OR (SELECT count(*) FROM bank_transactions) <> 2 THEN
    RAISE EXCEPTION 'same-day replay was not a no-op: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL bank interest bounds and ledger snapshot checks passed.'
