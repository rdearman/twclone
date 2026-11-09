-- REQUIREMENT_ID: V3-ECO-02
-- Interest ledger rows must carry the account balance produced by the same
-- settlement transaction.  Migration 152 rejects missing/low values, but a
-- fabricated higher value would still pass; this trigger closes that gap for
-- new writes while preserving legacy rows for upgrade diagnostics.
BEGIN;

CREATE OR REPLACE FUNCTION enforce_bank_interest_balance_after()
RETURNS trigger
LANGUAGE plpgsql
AS $$
DECLARE
  v_balance bigint;
BEGIN
  IF NEW.tx_type = 'INTEREST' THEN
    SELECT balance INTO v_balance
      FROM bank_accounts
     WHERE bank_accounts_id = NEW.account_id
     FOR KEY SHARE;
    IF NOT FOUND THEN
      RAISE EXCEPTION 'BANK_ACCOUNT_NOT_FOUND:%', NEW.account_id
        USING ERRCODE = '23503';
    END IF;
    IF NEW.balance_after IS DISTINCT FROM v_balance THEN
      RAISE EXCEPTION 'BANK_INTEREST_BALANCE_AFTER_MISMATCH:%:%',
        NEW.account_id, v_balance
        USING ERRCODE = '23514';
    END IF;
  END IF;
  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS bank_interest_balance_after_guard ON bank_transactions;
CREATE TRIGGER bank_interest_balance_after_guard
BEFORE INSERT OR UPDATE OF account_id, tx_type, balance_after
ON bank_transactions
FOR EACH ROW EXECUTE FUNCTION enforce_bank_interest_balance_after();

-- Stable evidence for the latest interest settlement per account.  Historical
-- rows are retained, so only the latest row is comparable to the current
-- account balance; legacy drift remains visible instead of being rewritten.
CREATE OR REPLACE VIEW bank_interest_balance_reconciliation_snapshot AS
WITH latest AS (
  SELECT DISTINCT ON (t.account_id)
         t.account_id, t.balance_after
    FROM bank_transactions t
   WHERE t.tx_type = 'INTEREST'
   ORDER BY t.account_id, t.ts DESC, t.bank_transactions_id DESC
)
SELECT
  (SELECT count(*)
     FROM latest l
     JOIN bank_accounts a ON a.bank_accounts_id = l.account_id
    WHERE l.balance_after IS DISTINCT FROM a.balance)
    AS latest_balance_mismatches;

COMMIT;
