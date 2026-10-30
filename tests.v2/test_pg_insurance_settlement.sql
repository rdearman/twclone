\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA insurance_124_test;
SET LOCAL search_path TO insurance_124_test, pg_catalog;
CREATE TYPE db_result AS (code integer, message text, id bigint);
CREATE TABLE insurance_funds (fund_id bigint PRIMARY KEY, balance bigint NOT NULL CHECK (balance >= 0));
CREATE TABLE bank_accounts (bank_accounts_id integer PRIMARY KEY, owner_type text, owner_id bigint,
  currency text, balance bigint NOT NULL CHECK (balance >= 0), is_active boolean NOT NULL DEFAULT TRUE);
CREATE TABLE bank_transactions (bank_transactions_id serial PRIMARY KEY, account_id integer,
  tx_type text, direction text, amount bigint CHECK (amount > 0), currency text,
  tx_group_id text, description text, ts bigint);
CREATE TABLE insurance_policies (insurance_policies_id bigserial PRIMARY KEY, holder_type text,
  holder_id bigint, subject_type text, subject_id bigint, premium bigint, payout bigint,
  fund_id bigint, start_ts text, expiry_ts text, active boolean NOT NULL DEFAULT TRUE);
CREATE TABLE insurance_claims (insurance_claims_id bigserial PRIMARY KEY, policy_id bigint,
  event_id text, amount bigint, status text NOT NULL DEFAULT 'open', ts timestamptz DEFAULT now(), paid_bank_tx bigint);
CREATE OR REPLACE FUNCTION ledger_balance() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  UPDATE bank_accounts SET balance = CASE WHEN NEW.direction='DEBIT' THEN balance-NEW.amount ELSE balance+NEW.amount END
   WHERE bank_accounts_id=NEW.account_id;
  RETURN NEW;
END $$;
CREATE TRIGGER ledger_balance AFTER INSERT ON bank_transactions FOR EACH ROW EXECUTE FUNCTION ledger_balance();
\ir ../sql/pg/124_insurance_settlement.sql
SET search_path TO insurance_124_test, pg_catalog;
INSERT INTO insurance_funds VALUES (1, 0), (2, 5);
INSERT INTO bank_accounts VALUES (10, 'player', 7, 'CRD', 100, TRUE);
DO $test$
DECLARE r db_result; p bigint; c bigint;
BEGIN
  SELECT * INTO r FROM insurance_buy_policy('player',7,'ship',42,25,80,1,'2026-01-01',NULL);
  IF r.code <> 0 THEN RAISE EXCEPTION 'buy failed: %', r; END IF;
  p := r.id;
  IF (SELECT balance FROM bank_accounts WHERE bank_accounts_id=10) <> 75
     OR (SELECT balance FROM insurance_funds WHERE fund_id=1) <> 25 THEN
    RAISE EXCEPTION 'premium accounting mismatch';
  END IF;
  INSERT INTO insurance_claims(policy_id,event_id,amount) VALUES (p,NULL,20) RETURNING insurance_claims_id INTO c;
  SELECT * INTO r FROM insurance_settle_claim(c,'destroyed-1',20);
  IF r.code <> 0 OR (SELECT balance FROM bank_accounts WHERE bank_accounts_id=10) <> 95
     OR (SELECT balance FROM insurance_funds WHERE fund_id=1) <> 5 THEN
    RAISE EXCEPTION 'claim settlement mismatch: %', r;
  END IF;
  SELECT * INTO r FROM insurance_settle_claim(c,'destroyed-1',20);
  IF r.code <> 0 OR (SELECT count(*) FROM bank_transactions WHERE direction='CREDIT') <> 1 THEN
    RAISE EXCEPTION 'claim idempotency failed: %', r;
  END IF;
END
$test$;
DO $test$
DECLARE r db_result; before_balance bigint;
BEGIN
  before_balance := (SELECT balance FROM bank_accounts WHERE bank_accounts_id=10);
  SELECT * INTO r FROM insurance_buy_policy('player',7,'ship',99,1000,1,2,'2026-01-01',NULL);
  IF r.code <> 402 OR (SELECT balance FROM bank_accounts WHERE bank_accounts_id=10) <> before_balance
     OR (SELECT count(*) FROM insurance_policies WHERE subject_id=99) <> 0 THEN
    RAISE EXCEPTION 'insufficient premium was not atomic: %', r;
  END IF;
END
$test$;
ROLLBACK;
\echo 'PostgreSQL insurance settlement checks passed.'
