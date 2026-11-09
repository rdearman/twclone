-- V3-ECO-02: compare the daily interest watermark in the same UTC domain
-- used by the ledger group, regardless of the session timezone.
BEGIN;

CREATE OR REPLACE FUNCTION bank_apply_interest()
RETURNS db_result
LANGUAGE plpgsql
AS $$
DECLARE
  v_policy bank_interest_policy%ROWTYPE;
  v_account RECORD;
  v_interest bigint;
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

  -- The ledger key is explicitly UTC; the watermark must use that same day
  -- boundary or a timezone-specific retry can attempt a duplicate credit.
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
    IF v_interest <= 0 THEN
      CONTINUE;
    END IF;
    IF v_interest > v_policy.max_balance - v_account.balance THEN
      v_interest := v_policy.max_balance - v_account.balance;
    END IF;
    IF v_interest <= 0 THEN
      CONTINUE;
    END IF;

    INSERT INTO bank_transactions
      (account_id, tx_type, direction, amount, currency, tx_group_id,
       description, ts)
    VALUES
      (v_account.bank_accounts_id, 'INTEREST', 'CREDIT', v_interest,
       v_policy.currency, v_group, 'daily bank interest',
       EXTRACT(EPOCH FROM v_today)::bigint);
    v_applied := v_applied + 1;
  END LOOP;

  UPDATE bank_interest_policy SET last_run_at = v_today
   WHERE bank_interest_policy_id = 1;
  RETURN (0, 'interest applied'::text, v_applied::bigint);
END;
$$;

-- Read-only evidence that every interest group encodes the UTC day of its
-- ledger timestamp.  This catches hand-written/replayed rows that bypass the
-- authoritative routine without rewriting legacy data.
CREATE OR REPLACE VIEW bank_interest_utc_invariant_snapshot AS
SELECT
  (SELECT count(*)
     FROM bank_transactions
    WHERE tx_type = 'INTEREST'
      AND tx_group_id !~ '^bank-interest-[0-9]{4}-[0-9]{2}-[0-9]{2}$')
    AS malformed_utc_groups,
  (SELECT count(*)
     FROM bank_transactions
    WHERE tx_type = 'INTEREST'
      AND tx_group_id ~ '^bank-interest-[0-9]{4}-[0-9]{2}-[0-9]{2}$'
      AND substring(tx_group_id FROM 15)::date IS DISTINCT FROM
          (to_timestamp(ts) AT TIME ZONE 'UTC')::date)
    AS utc_group_timestamp_mismatches;

COMMIT;
