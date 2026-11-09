\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA bank_interest_ledger_129_test;
SET LOCAL search_path TO bank_interest_ledger_129_test, pg_catalog;

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

\ir ../sql/pg/129_bank_interest_ledger_invariants.sql
SET search_path TO bank_interest_ledger_129_test, pg_catalog;

INSERT INTO bank_accounts VALUES (1, 'CRD');
INSERT INTO bank_transactions
  (account_id, tx_type, direction, amount, currency, tx_group_id)
VALUES (1, 'INTEREST', 'CREDIT', 25, 'CRD', 'bank-interest-2026-10-30');

DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM bank_interest_ledger_invariant_snapshot;
  IF r.malformed_interest_transactions <> 0
     OR r.interest_currency_mismatches <> 0
     OR r.duplicate_interest_groups <> 0 THEN
    RAISE EXCEPTION 'clean interest ledger snapshot reported violations: %', r;
  END IF;

  BEGIN
    INSERT INTO bank_transactions
      (account_id, tx_type, direction, amount, currency, tx_group_id)
    VALUES (1, 'INTEREST', 'CREDIT', 25, 'CRD', 'bank-interest-2026-10-30');
    RAISE EXCEPTION 'duplicate daily interest accepted';
  EXCEPTION WHEN unique_violation THEN NULL;
  END;

  BEGIN
    INSERT INTO bank_transactions
      (account_id, tx_type, direction, amount, currency, tx_group_id)
    VALUES (1, 'INTEREST', 'DEBIT', 1, 'USD', 'bank-interest-invalid');
    RAISE EXCEPTION 'malformed interest ledger write accepted';
  EXCEPTION WHEN check_violation THEN NULL;
  END;

  BEGIN
    INSERT INTO bank_transactions
      (account_id, tx_type, direction, amount, currency, tx_group_id)
    VALUES (1, 'INTEREST', 'CREDIT', 1, 'CRD', NULL);
    RAISE EXCEPTION 'NULL interest group accepted';
  EXCEPTION WHEN check_violation THEN NULL;
  END;

  SELECT * INTO r FROM bank_interest_ledger_invariant_snapshot;
  IF r.malformed_interest_transactions <> 0
     OR r.interest_currency_mismatches <> 0
     OR r.duplicate_interest_groups <> 0 THEN
    RAISE EXCEPTION 'rejected interest writes changed the snapshot: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL bank interest ledger invariant checks passed.'
