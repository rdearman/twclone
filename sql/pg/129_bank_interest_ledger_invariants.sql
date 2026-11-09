-- V3-ECO-02: make the daily interest ledger idempotency contract observable.
-- The engine and bank procedure must use one group per UTC day and one credit
-- per account in that group.  The unique index protects retries; the view is
-- a durable invariant snapshot for isolated and restart tests.
BEGIN;

-- Do not make an upgrade fail merely because legacy data already contains a
-- duplicate group.  The snapshot below must remain available to report that
-- drift.  A native index is installed when the existing data is clean; the
-- deferred guard below covers the legacy-drift case until it is repaired.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1
      FROM bank_transactions
     WHERE tx_type = 'INTEREST' AND tx_group_id IS NOT NULL
     GROUP BY account_id, tx_group_id
    HAVING count(*) > 1
  ) THEN
    CREATE UNIQUE INDEX IF NOT EXISTS ux_bank_interest_account_group
      ON bank_transactions (account_id, tx_group_id)
      WHERE tx_type = 'INTEREST' AND tx_group_id IS NOT NULL;
  END IF;
END $$;

CREATE OR REPLACE FUNCTION enforce_bank_interest_account_group()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
  IF NEW.tx_type = 'INTEREST' AND NEW.tx_group_id IS NOT NULL THEN
    PERFORM pg_advisory_xact_lock(
      hashtextextended(
        'bank-interest:' || NEW.account_id::text || ':' || NEW.tx_group_id,
        0
      )
    );
    IF EXISTS (
      SELECT 1
        FROM bank_transactions t
       WHERE t.account_id = NEW.account_id
         AND t.tx_group_id = NEW.tx_group_id
         AND t.tx_type = 'INTEREST'
         AND t.bank_transactions_id <> NEW.bank_transactions_id
    ) THEN
      RAISE EXCEPTION 'BANK_INTEREST_DUPLICATE:%:%',
        NEW.account_id, NEW.tx_group_id
        USING ERRCODE = '23505';
    END IF;
  END IF;
  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS bank_interest_account_group_guard
  ON bank_transactions;
CREATE CONSTRAINT TRIGGER bank_interest_account_group_guard
AFTER INSERT OR UPDATE OF account_id, tx_type, tx_group_id ON bank_transactions
DEFERRABLE INITIALLY DEFERRED
FOR EACH ROW EXECUTE FUNCTION enforce_bank_interest_account_group();

-- Preserve legacy rows for the snapshot below, but reject malformed interest
-- writes going forward.  The daily group is the idempotency key used by the
-- authoritative routine; allowing NULL here would make retries unbounded.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
     WHERE conname = 'bank_interest_ledger_shape'
       AND conrelid = 'bank_transactions'::regclass
  ) THEN
    ALTER TABLE bank_transactions
      ADD CONSTRAINT bank_interest_ledger_shape CHECK (
        tx_type <> 'INTEREST'
        OR (direction = 'CREDIT' AND amount > 0 AND tx_group_id IS NOT NULL)
      ) NOT VALID;
  END IF;
END $$;

CREATE OR REPLACE VIEW bank_interest_ledger_invariant_snapshot AS
SELECT
  (SELECT count(*)
     FROM bank_transactions
    WHERE tx_type = 'INTEREST'
      AND (direction IS DISTINCT FROM 'CREDIT'
           OR tx_group_id IS NULL
           OR amount <= 0)) AS malformed_interest_transactions,
  (SELECT count(*)
     FROM bank_transactions t
     JOIN bank_accounts a ON a.bank_accounts_id = t.account_id
    WHERE t.tx_type = 'INTEREST'
      AND t.currency IS DISTINCT FROM a.currency) AS interest_currency_mismatches,
  (SELECT count(*)
     FROM (
       SELECT account_id, tx_group_id
         FROM bank_transactions
        WHERE tx_type = 'INTEREST' AND tx_group_id IS NOT NULL
        GROUP BY account_id, tx_group_id
       HAVING count(*) > 1
     ) duplicate_interest_groups) AS duplicate_interest_groups;

COMMIT;
