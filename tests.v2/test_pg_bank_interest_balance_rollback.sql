\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-ECO-02
-- Migration 149 settles the account directly, so a failed ledger insert must
-- still roll back the preceding account update and the daily watermark.  This
-- fixture deliberately has no ledger trigger and never touches a live catalog.
BEGIN;
CREATE SCHEMA bank_interest_balance_rollback_149_test;
SET LOCAL search_path TO bank_interest_balance_rollback_149_test, pg_catalog;

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
  amount bigint NOT NULL CHECK (amount > 0 AND amount < 100),
  currency text NOT NULL,
  tx_group_id text,
  description text,
  ts bigint NOT NULL,
  balance_after bigint
);

\ir ../sql/pg/149_bank_interest_balance_ledger.sql
SET search_path TO bank_interest_balance_rollback_149_test, pg_catalog;
INSERT INTO bank_interest_policy
  VALUES (1, 365, 100, 1000000, NULL, 'none', 'CRD');
INSERT INTO bank_accounts VALUES (1, 10000, 0, 'CRD', TRUE);

DO $test$
DECLARE
  failed boolean := FALSE;
  snapshot record;
BEGIN
  BEGIN
    PERFORM bank_apply_interest();
  EXCEPTION WHEN check_violation THEN
    failed := TRUE;
  END;

  IF NOT failed THEN
    RAISE EXCEPTION 'invalid interest ledger row was unexpectedly accepted';
  END IF;
  IF (SELECT balance FROM bank_accounts WHERE bank_accounts_id = 1) <> 10000
     OR (SELECT count(*) FROM bank_transactions) <> 0
     OR (SELECT last_run_at FROM bank_interest_policy
           WHERE bank_interest_policy_id = 1) IS NOT NULL THEN
    RAISE EXCEPTION 'interest rollback leaked account, ledger, or watermark state';
  END IF;

  SELECT * INTO snapshot FROM bank_interest_balance_ledger_snapshot;
  IF snapshot.missing_or_invalid_balance_after <> 0
     OR snapshot.interest_currency_mismatches <> 0 THEN
    RAISE EXCEPTION 'rollback left balance-ledger invariant drift: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL bank-interest balance rollback checks passed.'
