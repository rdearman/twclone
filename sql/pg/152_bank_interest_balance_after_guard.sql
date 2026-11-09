-- REQUIREMENT_ID: V3-ECO-02
-- Interest settlement records the account balance and ledger entry together.
-- Preserve pre-existing drift for diagnostics, but reject new interest rows
-- that omit the post-settlement balance used by restart reconciliation.
BEGIN;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1
      FROM pg_constraint
     WHERE conname = 'bank_interest_balance_after_required'
       AND conrelid = 'bank_transactions'::regclass
  ) THEN
    ALTER TABLE bank_transactions
      ADD CONSTRAINT bank_interest_balance_after_required CHECK (
        tx_type <> 'INTEREST'
        OR (balance_after IS NOT NULL AND balance_after >= amount)
      ) NOT VALID;
  END IF;
END $$;

CREATE OR REPLACE VIEW bank_interest_balance_ledger_snapshot AS
SELECT
  (SELECT count(*)
     FROM bank_transactions
    WHERE tx_type = 'INTEREST'
      AND (balance_after IS NULL OR balance_after < amount))
    AS missing_or_invalid_balance_after,
  (SELECT count(*)
     FROM bank_transactions t
     JOIN bank_accounts a ON a.bank_accounts_id = t.account_id
    WHERE t.tx_type = 'INTEREST'
      AND t.direction = 'CREDIT'
      AND t.currency IS DISTINCT FROM a.currency)
    AS interest_currency_mismatches;

COMMIT;
