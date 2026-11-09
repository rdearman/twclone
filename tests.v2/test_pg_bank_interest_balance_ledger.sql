\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-ECO-02
-- Prove that the authoritative SQL path changes the account and records the
-- resulting balance in the same transaction.  No ledger trigger is installed:
-- a trigger-backed fixture would hide a persistence defect in the routine.
BEGIN;
CREATE SCHEMA bank_interest_balance_149_test;
SET LOCAL search_path TO bank_interest_balance_149_test, pg_catalog;

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
  ts bigint NOT NULL,
  balance_after bigint
);

\ir ../sql/pg/149_bank_interest_balance_ledger.sql
SET search_path TO bank_interest_balance_149_test, pg_catalog;
INSERT INTO bank_interest_policy
  VALUES (1, 365, 100, 1000000, NULL, 'none', 'CRD');
INSERT INTO bank_accounts VALUES (1, 10000, 0, 'CRD', TRUE);

DO $test$
DECLARE
  r db_result;
  snapshot record;
BEGIN
  SELECT * INTO r FROM bank_apply_interest();
  IF r.code <> 0 OR r.id <> 1 THEN
    RAISE EXCEPTION 'interest application failed: %', r;
  END IF;
  IF (SELECT balance FROM bank_accounts WHERE bank_accounts_id = 1) <> 10100
     OR (SELECT amount FROM bank_transactions) <> 100
     OR (SELECT balance_after FROM bank_transactions) <> 10100 THEN
    RAISE EXCEPTION 'account and ledger did not settle together';
  END IF;
  SELECT * INTO snapshot FROM bank_interest_balance_ledger_snapshot;
  IF snapshot.missing_or_invalid_balance_after <> 0
     OR snapshot.interest_currency_mismatches <> 0 THEN
    RAISE EXCEPTION 'interest balance/ledger snapshot is not clean: %', snapshot;
  END IF;

  SELECT * INTO r FROM bank_apply_interest();
  IF r.code <> 0 OR r.id <> 0
     OR (SELECT balance FROM bank_accounts WHERE bank_accounts_id = 1) <> 10100
     OR (SELECT count(*) FROM bank_transactions) <> 1 THEN
    RAISE EXCEPTION 'same-day replay changed settled state: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL bank-interest balance/ledger checks passed.'
