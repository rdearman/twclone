-- Keep the insurance audit view aligned with the guaranteed settlement
-- transaction type introduced by migration 175. Guaranteed claims are valid
-- credits, not malformed ADJUSTMENT entries.
BEGIN;

CREATE OR REPLACE VIEW insurance_ledger_invariant_snapshot AS
SELECT
  (SELECT count(*)
     FROM insurance_policies p
    WHERE p.premium > 0
      AND NOT EXISTS (
        SELECT 1
          FROM bank_transactions t
         WHERE t.tx_group_id = 'insurance-policy-' || p.insurance_policies_id
           AND t.tx_type = 'FEE'
           AND t.direction = 'DEBIT'
           AND t.amount = p.premium
           AND t.currency = 'CRD'
      )) AS missing_premium_entries,
  (SELECT count(*)
     FROM insurance_claims c
    WHERE c.status = 'paid'
      AND c.amount > 0
      AND NOT EXISTS (
        SELECT 1
          FROM bank_transactions t
         WHERE t.tx_group_id = 'insurance-claim-' || c.insurance_claims_id
           AND t.tx_type IN ('ADJUSTMENT', 'INSURANCE_GUARANTEE')
           AND t.direction = 'CREDIT'
           AND t.amount = c.amount
           AND t.currency = 'CRD'
      )) AS missing_claim_entries,
  (SELECT count(*)
     FROM bank_transactions t
    WHERE t.tx_group_id LIKE 'insurance-policy-%'
      AND (t.tx_type <> 'FEE' OR t.direction <> 'DEBIT' OR t.currency <> 'CRD'))
    AS malformed_premium_entries,
  (SELECT count(*)
     FROM bank_transactions t
    WHERE t.tx_group_id LIKE 'insurance-claim-%'
      AND (t.tx_type NOT IN ('ADJUSTMENT', 'INSURANCE_GUARANTEE')
           OR t.direction <> 'CREDIT' OR t.currency <> 'CRD'))
    AS malformed_claim_entries,
  (SELECT count(*)
     FROM (
       SELECT tx_group_id
         FROM bank_transactions
        WHERE tx_group_id LIKE 'insurance-policy-%'
        GROUP BY tx_group_id
       HAVING count(*) <> 1
     ) duplicate_premiums) AS duplicate_premium_groups,
  (SELECT count(*)
     FROM (
       SELECT tx_group_id
         FROM bank_transactions
        WHERE tx_group_id LIKE 'insurance-claim-%'
        GROUP BY tx_group_id
       HAVING count(*) <> 1
     ) duplicate_claims) AS duplicate_claim_groups;

COMMIT;
