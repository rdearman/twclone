\set ON_ERROR_STOP on

-- V3-ECO-02: legacy duplicate interest groups must be observable during an
-- upgrade, while every new duplicate remains rejected and race-serialized.
BEGIN;
CREATE SCHEMA bank_interest_legacy_129_test;
SET LOCAL search_path TO bank_interest_legacy_129_test, pg_catalog;

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
  tx_group_id text
);

INSERT INTO bank_accounts VALUES (1, 'CRD');
INSERT INTO bank_transactions
  (account_id, tx_type, direction, amount, currency, tx_group_id)
VALUES
  (1, 'INTEREST', 'CREDIT', 25, 'CRD', 'bank-interest-2026-10-30'),
  (1, 'INTEREST', 'CREDIT', 25, 'CRD', 'bank-interest-2026-10-30');

-- Applying the migration must not fail before its diagnostic view is usable.
\ir ../sql/pg/129_bank_interest_ledger_invariants.sql
SET search_path TO bank_interest_legacy_129_test, pg_catalog;

DO $test$
DECLARE
  r record;
BEGIN
  SELECT * INTO r FROM bank_interest_ledger_invariant_snapshot;
  IF r.duplicate_interest_groups <> 1 THEN
    RAISE EXCEPTION 'legacy duplicate was not reported: %', r;
  END IF;

  BEGIN
    INSERT INTO bank_transactions
      (account_id, tx_type, direction, amount, currency, tx_group_id)
    VALUES (1, 'INTEREST', 'CREDIT', 25, 'CRD', 'bank-interest-2026-10-30');
    SET CONSTRAINTS ALL IMMEDIATE;
    RAISE EXCEPTION 'new duplicate interest group was accepted';
  EXCEPTION WHEN unique_violation THEN NULL;
  END;

  IF (SELECT count(*) FROM bank_transactions) <> 2 THEN
    RAISE EXCEPTION 'rejected duplicate changed legacy ledger';
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL bank interest legacy duplicate checks passed.'
