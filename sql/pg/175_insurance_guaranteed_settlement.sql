-- Pending qualifying claims created before the V3 guarantee are payable even
-- when their insurer reserve is empty. System-created value is labeled in the
-- bank ledger and the claim row is the exactly-once guard.
CREATE OR REPLACE FUNCTION insurance_settle_claim(
  p_claim_id bigint, p_event_id text, p_amount bigint
) RETURNS db_result LANGUAGE plpgsql AS $$
DECLARE
  v_claim insurance_claims%ROWTYPE;
  v_policy insurance_policies%ROWTYPE;
  v_account integer;
  v_balance bigint;
  v_fund_balance bigint := 0;
  v_funded boolean := false;
  v_tx_id bigint;
  v_ts bigint := EXTRACT(EPOCH FROM CURRENT_TIMESTAMP)::bigint;
BEGIN
  IF p_claim_id <= 0 OR p_amount <= 0 OR p_event_id IS NULL OR length(p_event_id)=0 THEN
    RETURN (400, 'invalid insurance claim'::text, NULL::bigint);
  END IF;
  SELECT * INTO v_claim FROM insurance_claims
   WHERE insurance_claims_id=p_claim_id FOR UPDATE;
  IF NOT FOUND THEN RETURN (404, 'claim not found'::text, NULL::bigint); END IF;
  IF v_claim.status='paid' THEN RETURN (0, 'claim already paid'::text, p_claim_id); END IF;
  SELECT * INTO v_policy FROM insurance_policies
   WHERE insurance_policies_id=v_claim.policy_id FOR UPDATE;
  IF NOT FOUND OR NOT v_policy.active THEN
    RETURN (409, 'policy inactive'::text, NULL::bigint);
  END IF;
  IF p_amount>v_policy.payout OR p_amount>v_claim.amount THEN
    RETURN (409, 'claim exceeds policy payout'::text, NULL::bigint);
  END IF;
  SELECT bank_accounts_id,balance INTO v_account,v_balance FROM bank_accounts
   WHERE owner_type=v_policy.holder_type AND owner_id=v_policy.holder_id
     AND currency='CRD' AND is_active=TRUE FOR UPDATE;
  IF NOT FOUND THEN RETURN (404, 'holder account not found'::text, NULL::bigint); END IF;
  IF v_balance > 9223372036854775807-p_amount THEN
    RETURN (409, 'insurance payout exceeds account capacity'::text, NULL::bigint);
  END IF;
  IF v_policy.fund_id IS NOT NULL THEN
    SELECT balance INTO v_fund_balance FROM insurance_funds
     WHERE fund_id=v_policy.fund_id FOR UPDATE;
    IF FOUND AND v_fund_balance>=p_amount THEN
      UPDATE insurance_funds SET balance=balance-p_amount
       WHERE fund_id=v_policy.fund_id AND balance>=p_amount;
      v_funded := FOUND;
    END IF;
  END IF;
  UPDATE bank_accounts SET balance=balance+p_amount
   WHERE bank_accounts_id=v_account AND is_active=TRUE;
  INSERT INTO bank_transactions(account_id,tx_type,direction,amount,currency,
      tx_group_id,description,ts,balance_after,idempotency_key)
   VALUES(v_account,CASE WHEN v_funded THEN 'ADJUSTMENT' ELSE 'INSURANCE_GUARANTEE' END,
      'CREDIT',p_amount,'CRD','insurance-claim-'||p_claim_id,
      CASE WHEN v_funded THEN 'automatic hull insurance'
           ELSE 'system-guaranteed hull insurance' END,
      v_ts,v_balance+p_amount,'insurance-claim-settlement-'||p_claim_id)
   RETURNING bank_transactions_id INTO v_tx_id;
  UPDATE insurance_claims SET status='paid',event_id=p_event_id,paid_bank_tx=v_tx_id
   WHERE insurance_claims_id=p_claim_id AND status='open';
  IF NOT FOUND THEN RAISE EXCEPTION 'insurance claim state changed during settlement'; END IF;
  RETURN (0,'claim paid',p_claim_id);
END;
$$;
