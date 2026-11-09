\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-ECO-02
-- Isolated catalog test for the interest ledger's restart/reconciliation
-- balance assertion.  No production catalog or live scheduler is touched.
BEGIN;
CREATE SCHEMA bank_interest_balance_after_152_test;
SET LOCAL search_path TO bank_interest_balance_after_152_test, pg_catalog;

CREATE TABLE bank_accounts (
  bank_accounts_id integer PRIMARY KEY,
  currency text NOT NULL
);
CREATE TABLE bank_transactions (
  bank_transactions_id serial PRIMARY KEY,
  account_id integer NOT NULL REFERENCES bank_accounts(bank_accounts_id),
  tx_type text NOT NULL,
  direction text NOT NULL,
  amount bigint NOT NULL CHECK (amount > 0),
  currency text NOT NULL,
  balance_after bigint
);

\ir ../sql/pg/152_bank_interest_balance_after_guard.sql
SET search_path TO bank_interest_balance_after_152_test, pg_catalog;
INSERT INTO bank_accounts VALUES (1, 'CRD');

DO $test$
DECLARE
  snapshot record;
  rejected boolean := FALSE;
BEGIN
  INSERT INTO bank_transactions
    (account_id, tx_type, direction, amount, currency, balance_after)
  VALUES (1, 'INTEREST', 'CREDIT', 100, 'CRD', 1100);

  SELECT * INTO snapshot FROM bank_interest_balance_ledger_snapshot;
  IF snapshot.missing_or_invalid_balance_after <> 0
     OR snapshot.interest_currency_mismatches <> 0 THEN
    RAISE EXCEPTION 'valid interest ledger snapshot is dirty: %', snapshot;
  END IF;

  BEGIN
    INSERT INTO bank_transactions
      (account_id, tx_type, direction, amount, currency, balance_after)
    VALUES (1, 'INTEREST', 'CREDIT', 100, 'CRD', NULL);
  EXCEPTION WHEN check_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'interest row without balance_after was accepted';
  END IF;

  rejected := FALSE;
  BEGIN
    INSERT INTO bank_transactions
      (account_id, tx_type, direction, amount, currency, balance_after)
    VALUES (1, 'INTEREST', 'CREDIT', 100, 'CRD', 99);
  EXCEPTION WHEN check_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'interest row with balance_after below amount was accepted';
  END IF;

  SELECT * INTO snapshot FROM bank_interest_balance_ledger_snapshot;
  IF snapshot.missing_or_invalid_balance_after <> 0 THEN
    RAISE EXCEPTION 'rejected interest writes left ledger drift: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL bank-interest balance-after guard checks passed.'
