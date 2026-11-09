-- V3-ECO-02: settle SQL bank interest through the account and ledger together.
-- The earlier routine relied on a test-only ledger trigger to move the account
-- balance.  Production PostgreSQL has no such trigger, so an interest row could
-- be recorded without changing the account.  Keep the account row lock held,
-- update the balance, and write the resulting balance_after in one transaction.
BEGIN;

CREATE OR REPLACE FUNCTION bank_apply_interest()
RETURNS db_result
LANGUAGE plpgsql
AS $$
DECLARE
  v_policy bank_interest_policy%ROWTYPE;
  v_account RECORD;
  v_interest bigint;
  v_balance_after bigint;
  v_applied integer := 0;
  v_today timestamptz := CURRENT_TIMESTAMP;
  v_group text;
BEGIN
  SELECT * INTO v_policy
    FROM bank_interest_policy
   WHERE bank_interest_policy_id = 1
   FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'bank interest policy not found'::text, NULL::bigint);
  END IF;

  IF v_policy.last_run_at IS NOT NULL
     AND (v_policy.last_run_at AT TIME ZONE 'UTC')::date >=
         (v_today AT TIME ZONE 'UTC')::date THEN
    RETURN (0, 'interest already applied'::text, 0::bigint);
  END IF;

  IF v_policy.apr_bps <= 0 THEN
    UPDATE bank_interest_policy SET last_run_at = v_today
     WHERE bank_interest_policy_id = 1;
    RETURN (0, 'interest disabled'::text, 0::bigint);
  END IF;

  v_group := 'bank-interest-' || to_char(v_today AT TIME ZONE 'UTC', 'YYYY-MM-DD');
  FOR v_account IN
    SELECT bank_accounts_id, balance, interest_rate_bp
      FROM bank_accounts
     WHERE is_active = TRUE
       AND currency = v_policy.currency
       AND balance >= v_policy.min_balance
       AND balance < v_policy.max_balance
     ORDER BY bank_accounts_id
     FOR UPDATE
  LOOP
    v_interest := floor((v_account.balance::numeric
                         * GREATEST(v_account.interest_rate_bp, v_policy.apr_bps)::numeric)
                        / 36500)::bigint;
    v_interest := LEAST(v_interest, v_policy.max_balance - v_account.balance);
    IF v_interest <= 0 THEN
      CONTINUE;
    END IF;

    UPDATE bank_accounts
       SET balance = balance + v_interest
     WHERE bank_accounts_id = v_account.bank_accounts_id
     RETURNING balance INTO v_balance_after;

    INSERT INTO bank_transactions
      (account_id, tx_type, direction, amount, currency, tx_group_id,
       description, ts, balance_after)
    VALUES
      (v_account.bank_accounts_id, 'INTEREST', 'CREDIT', v_interest,
       v_policy.currency, v_group, 'daily bank interest',
       EXTRACT(EPOCH FROM v_today)::bigint, v_balance_after);
    v_applied := v_applied + 1;
  END LOOP;

  UPDATE bank_interest_policy SET last_run_at = v_today
   WHERE bank_interest_policy_id = 1;
  RETURN (0, 'interest applied'::text, v_applied::bigint);
END;
$$;

CREATE OR REPLACE VIEW bank_interest_balance_ledger_snapshot AS
SELECT
  (SELECT count(*)
     FROM bank_transactions
    WHERE tx_type = 'INTEREST'
      AND (balance_after IS NULL OR balance_after <= 0))
    AS missing_or_invalid_balance_after,
  (SELECT count(*)
     FROM bank_transactions t
     JOIN bank_accounts a ON a.bank_accounts_id = t.account_id
    WHERE t.tx_type = 'INTEREST'
      AND t.direction = 'CREDIT'
      AND t.currency IS DISTINCT FROM a.currency)
    AS interest_currency_mismatches;

COMMIT;
