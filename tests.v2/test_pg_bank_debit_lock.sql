\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA bank_lock_test;
SET LOCAL search_path TO bank_lock_test, pg_catalog;
CREATE TABLE bank_accounts (bank_accounts_id integer PRIMARY KEY, balance bigint NOT NULL CHECK (balance >= 0));
CREATE TABLE bank_transactions (bank_transactions_id serial PRIMARY KEY, account_id integer NOT NULL REFERENCES bank_accounts,
  direction text NOT NULL, amount bigint NOT NULL CHECK (amount > 0));
\ir ../sql/pg/121_bank_debit_lock.sql
CREATE TRIGGER bank_debit_guard BEFORE INSERT ON bank_transactions FOR EACH ROW EXECUTE FUNCTION fn_bank_transactions_before_insert();
CREATE OR REPLACE FUNCTION bank_balance_update() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN
  UPDATE bank_accounts SET balance = CASE WHEN NEW.direction='DEBIT' THEN balance-NEW.amount ELSE balance+NEW.amount END
   WHERE bank_accounts_id=NEW.account_id; RETURN NEW; END $$;
CREATE TRIGGER bank_balance_update AFTER INSERT ON bank_transactions FOR EACH ROW EXECUTE FUNCTION bank_balance_update();
INSERT INTO bank_accounts VALUES (1, 100);
INSERT INTO bank_transactions(account_id,direction,amount) VALUES (1,'DEBIT',100);
DO $test$
BEGIN
  BEGIN
    INSERT INTO bank_transactions(account_id,direction,amount) VALUES (1,'DEBIT',1);
    RAISE EXCEPTION 'debit beyond zero was accepted';
  EXCEPTION WHEN OTHERS THEN
    IF SQLERRM <> 'BANK_INSUFFICIENT_FUNDS' THEN RAISE; END IF;
  END;
END
$test$;
ROLLBACK;
\echo 'PostgreSQL bank debit locking checks passed.'
