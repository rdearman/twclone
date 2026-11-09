-- V3-INS-01: expose insurance ledger semantics without deciding canon scope.
-- If insurance is enabled, premiums are FEE/DEBIT entries and paid claims are
-- ADJUSTMENT/CREDIT entries.  The snapshot is intentionally read-only so a
-- live acceptance harness can report legacy or partial rows after restart.
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
           AND t.tx_type = 'ADJUSTMENT'
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
      AND (t.tx_type <> 'ADJUSTMENT' OR t.direction <> 'CREDIT' OR t.currency <> 'CRD'))
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
