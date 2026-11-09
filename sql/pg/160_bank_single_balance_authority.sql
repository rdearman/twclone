-- REQUIREMENT_ID: V3-ECO-02
-- The bank repository is the sole balance mutator.  The legacy 092 trigger
-- applied the ledger delta a second time after the repository update.
BEGIN;

CREATE OR REPLACE FUNCTION fn_bank_transactions_before_insert ()
RETURNS trigger
LANGUAGE plpgsql
AS $$
DECLARE
  current_bal bigint;
BEGIN
  SELECT balance INTO current_bal
    FROM bank_accounts
   WHERE bank_accounts_id = NEW.account_id
   FOR UPDATE;
  IF NOT FOUND THEN
    RAISE EXCEPTION 'BANK_ACCOUNT_NOT_FOUND';
  END IF;
  IF NEW.balance_after IS NULL OR NEW.balance_after < 0 THEN
    RAISE EXCEPTION 'BANK_INVALID_BALANCE_AFTER';
  END IF;
  RETURN NEW;
END;
$$;

CREATE OR REPLACE FUNCTION fn_bank_transactions_after_insert ()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
  UPDATE bank_transactions
     SET balance_after = (
       SELECT balance FROM bank_accounts
        WHERE bank_accounts_id = NEW.account_id)
   WHERE bank_transactions_id = NEW.bank_transactions_id;
  RETURN NEW;
END;
$$;

COMMIT;
