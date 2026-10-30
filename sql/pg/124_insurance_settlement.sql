-- Transactional insurance policy purchase and claim settlement primitives.
BEGIN;

CREATE UNIQUE INDEX IF NOT EXISTS ux_insurance_claim_event
  ON insurance_claims (policy_id, event_id) WHERE event_id IS NOT NULL;

CREATE OR REPLACE FUNCTION insurance_buy_policy(
  p_holder_type text, p_holder_id bigint, p_subject_type text,
  p_subject_id bigint, p_premium bigint, p_payout bigint,
  p_fund_id bigint, p_start_ts text, p_expiry_ts text DEFAULT NULL
) RETURNS db_result LANGUAGE plpgsql AS $$
DECLARE
  v_account integer;
  v_balance bigint;
  v_policy bigint;
  v_ts bigint := EXTRACT(EPOCH FROM CURRENT_TIMESTAMP)::bigint;
BEGIN
  IF p_holder_type NOT IN ('player','corp') OR p_subject_type NOT IN ('ship','cargo','planet')
     OR p_holder_id <= 0 OR p_subject_id <= 0 OR p_premium < 0 OR p_payout < 0 THEN
    RETURN (400, 'invalid insurance policy'::text, NULL::bigint);
  END IF;
  PERFORM 1 FROM insurance_funds WHERE fund_id = p_fund_id FOR UPDATE;
  IF NOT FOUND THEN RETURN (404, 'insurance fund not found'::text, NULL::bigint); END IF;
  SELECT bank_accounts_id, balance INTO v_account, v_balance
    FROM bank_accounts
   WHERE owner_type = p_holder_type AND owner_id = p_holder_id
     AND currency = 'CRD' AND is_active = TRUE FOR UPDATE;
  IF NOT FOUND THEN RETURN (404, 'holder account not found'::text, NULL::bigint); END IF;
  IF v_balance < p_premium THEN RETURN (402, 'insufficient funds'::text, NULL::bigint); END IF;

  INSERT INTO insurance_policies
    (holder_type, holder_id, subject_type, subject_id, premium, payout,
     fund_id, start_ts, expiry_ts, active)
  VALUES (p_holder_type, p_holder_id, p_subject_type, p_subject_id,
          p_premium, p_payout, p_fund_id, p_start_ts, p_expiry_ts, TRUE)
  RETURNING insurance_policies_id INTO v_policy;

  IF p_premium > 0 THEN
    INSERT INTO bank_transactions
      (account_id, tx_type, direction, amount, currency, tx_group_id, description, ts)
    VALUES (v_account, 'FEE', 'DEBIT', p_premium, 'CRD',
            'insurance-policy-' || v_policy, 'insurance premium', v_ts);
    UPDATE insurance_funds SET balance = balance + p_premium WHERE fund_id = p_fund_id;
  END IF;
  RETURN (0, 'policy purchased'::text, v_policy);
END;
$$;

CREATE OR REPLACE FUNCTION insurance_settle_claim(
  p_claim_id bigint, p_event_id text, p_amount bigint
) RETURNS db_result LANGUAGE plpgsql AS $$
DECLARE
  v_claim insurance_claims%ROWTYPE;
  v_policy insurance_policies%ROWTYPE;
  v_account integer;
  v_fund_balance bigint;
  v_tx_id bigint;
  v_ts bigint := EXTRACT(EPOCH FROM CURRENT_TIMESTAMP)::bigint;
BEGIN
  IF p_claim_id <= 0 OR p_amount < 0 OR p_event_id IS NULL OR length(p_event_id) = 0 THEN
    RETURN (400, 'invalid insurance claim'::text, NULL::bigint);
  END IF;
  SELECT * INTO v_claim FROM insurance_claims WHERE insurance_claims_id = p_claim_id FOR UPDATE;
  IF NOT FOUND THEN RETURN (404, 'claim not found'::text, NULL::bigint); END IF;
  IF v_claim.status = 'paid' THEN RETURN (0, 'claim already paid'::text, p_claim_id); END IF;
  SELECT * INTO v_policy FROM insurance_policies
   WHERE insurance_policies_id = v_claim.policy_id FOR UPDATE;
  IF NOT FOUND OR NOT v_policy.active THEN RETURN (409, 'policy inactive'::text, NULL::bigint); END IF;
  IF p_amount > v_policy.payout OR p_amount > v_claim.amount THEN
    RETURN (409, 'claim exceeds policy payout'::text, NULL::bigint);
  END IF;
  SELECT bank_accounts_id INTO v_account FROM bank_accounts
   WHERE owner_type = v_policy.holder_type AND owner_id = v_policy.holder_id
     AND currency = 'CRD' AND is_active = TRUE FOR UPDATE;
  IF NOT FOUND THEN RETURN (404, 'holder account not found'::text, NULL::bigint); END IF;
  SELECT balance INTO v_fund_balance FROM insurance_funds
   WHERE fund_id = v_policy.fund_id FOR UPDATE;
  IF NOT FOUND OR v_fund_balance < p_amount THEN
    RETURN (409, 'insurance fund insufficient'::text, NULL::bigint);
  END IF;
  UPDATE insurance_funds SET balance = balance - p_amount WHERE fund_id = v_policy.fund_id;
  IF p_amount > 0 THEN
    INSERT INTO bank_transactions
      (account_id, tx_type, direction, amount, currency, tx_group_id, description, ts)
    VALUES (v_account, 'INTEREST', 'CREDIT', p_amount, 'CRD',
            'insurance-claim-' || p_claim_id, 'insurance claim payout', v_ts)
    RETURNING bank_transactions_id INTO v_tx_id;
  END IF;
  UPDATE insurance_claims SET status = 'paid', event_id = p_event_id,
      paid_bank_tx = v_tx_id
    WHERE insurance_claims_id = p_claim_id;
  RETURN (0, 'claim settled'::text, p_claim_id);
END;
$$;

COMMIT;
