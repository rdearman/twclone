\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-ECO-02
-- Isolated proof that new interest rows must record the account balance from
-- the settlement transaction, and that the latest-row snapshot is clean.
BEGIN;
CREATE SCHEMA bank_interest_reconciliation_158_test;
SET LOCAL search_path TO bank_interest_reconciliation_158_test, pg_catalog;

CREATE TABLE bank_accounts (
  bank_accounts_id integer PRIMARY KEY,
  balance bigint NOT NULL CHECK (balance >= 0)
);
CREATE TABLE bank_transactions (
  bank_transactions_id serial PRIMARY KEY,
  account_id integer NOT NULL REFERENCES bank_accounts(bank_accounts_id),
  tx_type text NOT NULL,
  direction text NOT NULL,
  amount bigint NOT NULL CHECK (amount > 0),
  tx_group_id text,
  ts bigint NOT NULL,
  balance_after bigint
);

\ir ../sql/pg/158_bank_interest_balance_reconciliation.sql
SET search_path TO bank_interest_reconciliation_158_test, pg_catalog;
INSERT INTO bank_accounts VALUES (1, 1100);

DO $test$
DECLARE
  snapshot record;
  rejected boolean := FALSE;
BEGIN
  INSERT INTO bank_transactions
    (account_id, tx_type, direction, amount, tx_group_id, ts, balance_after)
  VALUES (1, 'INTEREST', 'CREDIT', 100, 'bank-interest-test', 10, 1100);

  SELECT * INTO snapshot FROM bank_interest_balance_reconciliation_snapshot;
  IF snapshot.latest_balance_mismatches <> 0 THEN
    RAISE EXCEPTION 'valid interest reconciliation is dirty: %', snapshot;
  END IF;

  BEGIN
    INSERT INTO bank_transactions
      (account_id, tx_type, direction, amount, tx_group_id, ts, balance_after)
    VALUES (1, 'INTEREST', 'CREDIT', 100, 'bank-interest-invalid', 11, 1200);
  EXCEPTION WHEN check_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'fabricated interest balance_after was accepted';
  END IF;

  SELECT * INTO snapshot FROM bank_interest_balance_reconciliation_snapshot;
  IF snapshot.latest_balance_mismatches <> 0
     OR (SELECT count(*) FROM bank_transactions) <> 1 THEN
    RAISE EXCEPTION 'rejected reconciliation write left drift: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL bank-interest balance reconciliation checks passed.'
