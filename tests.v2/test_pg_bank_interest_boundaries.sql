\set ON_ERROR_STOP on

-- V3-ECO-02: bounded interest must settle through the normal bank ledger,
-- preserve the account cap, and remain idempotent for the UTC-day group.
BEGIN;
CREATE SCHEMA bank_interest_boundary_test;
SET LOCAL search_path TO bank_interest_boundary_test, pg_catalog;

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
  ts bigint NOT NULL,
  balance_after bigint DEFAULT 0
);

-- Reproduce the production bank transaction settlement trigger in this
-- isolated fixture; the interest routine must use the same ledger path as all
-- other credits rather than mutating balances through a second mechanism.
CREATE OR REPLACE FUNCTION settle_credit() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  UPDATE bank_accounts
     SET balance = balance + NEW.amount
   WHERE bank_accounts_id = NEW.account_id;
  UPDATE bank_transactions
     SET balance_after = (SELECT balance FROM bank_accounts
                           WHERE bank_accounts_id = NEW.account_id)
   WHERE bank_transactions_id = NEW.bank_transactions_id;
  RETURN NEW;
END $$;
CREATE TRIGGER bank_credit AFTER INSERT ON bank_transactions
FOR EACH ROW EXECUTE FUNCTION settle_credit();

\ir ../sql/pg/122_bank_interest_atomic.sql
\ir ../sql/pg/129_bank_interest_ledger_invariants.sql
SET search_path TO bank_interest_boundary_test, pg_catalog;

INSERT INTO bank_interest_policy
  VALUES (1, 365, 100, 10050, NULL, 'none', 'CRD');
INSERT INTO bank_accounts VALUES
  (1, 10000, 0, 'CRD', TRUE), -- eligible; capped at 10050
  (2, 99, 0, 'CRD', TRUE),    -- below minimum
  (3, 10050, 0, 'CRD', TRUE); -- already at cap

DO $test$
DECLARE r db_result;
DECLARE snapshot record;
BEGIN
  SELECT * INTO r FROM bank_apply_interest();
  IF r.code <> 0 OR r.id <> 1 THEN
    RAISE EXCEPTION 'bounded interest did not report one applied account: %', r;
  END IF;
  IF (SELECT balance FROM bank_accounts WHERE bank_accounts_id = 1) <> 10050
     OR (SELECT amount FROM bank_transactions WHERE account_id = 1) <> 50
     OR (SELECT balance_after FROM bank_transactions WHERE account_id = 1) <> 10050 THEN
    RAISE EXCEPTION 'interest cap or balance-after settlement is incorrect';
  END IF;
  IF (SELECT count(*) FROM bank_transactions) <> 1
     OR (SELECT balance FROM bank_accounts WHERE bank_accounts_id = 2) <> 99
     OR (SELECT balance FROM bank_accounts WHERE bank_accounts_id = 3) <> 10050 THEN
    RAISE EXCEPTION 'ineligible accounts were changed';
  END IF;

  SELECT * INTO snapshot FROM bank_interest_ledger_invariant_snapshot;
  IF snapshot.malformed_interest_transactions <> 0
     OR snapshot.interest_currency_mismatches <> 0
     OR snapshot.duplicate_interest_groups <> 0 THEN
    RAISE EXCEPTION 'bounded interest left ledger invariant drift: %', snapshot;
  END IF;

  SELECT * INTO r FROM bank_apply_interest();
  IF r.code <> 0 OR r.id <> 0 OR (SELECT count(*) FROM bank_transactions) <> 1
     OR (SELECT balance FROM bank_accounts WHERE bank_accounts_id = 1) <> 10050 THEN
    RAISE EXCEPTION 'same-day bounded interest was not idempotent: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL bank interest boundary checks passed.'
