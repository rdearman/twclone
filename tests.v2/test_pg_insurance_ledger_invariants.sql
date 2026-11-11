\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA insurance_ledger_130_test;
SET LOCAL search_path TO insurance_ledger_130_test, pg_catalog;

CREATE TABLE insurance_policies (
  insurance_policies_id bigint PRIMARY KEY,
  premium bigint NOT NULL
);
CREATE TABLE insurance_claims (
  insurance_claims_id bigint PRIMARY KEY,
  amount bigint NOT NULL,
  status text NOT NULL
);
CREATE TABLE bank_transactions (
  bank_transactions_id bigserial PRIMARY KEY,
  tx_group_id text,
  tx_type text NOT NULL,
  direction text NOT NULL,
  amount bigint NOT NULL,
  currency text NOT NULL
);

\ir ../sql/pg/130_insurance_ledger_invariants.sql
SET search_path TO insurance_ledger_130_test, pg_catalog;
\ir ../sql/pg/179_insurance_guarantee_ledger_invariants.sql
SET search_path TO insurance_ledger_130_test, pg_catalog;

INSERT INTO insurance_policies VALUES (7, 25);
INSERT INTO insurance_claims VALUES (9, 20, 'paid'), (10, 15, 'paid');
INSERT INTO bank_transactions (tx_group_id, tx_type, direction, amount, currency)
VALUES ('insurance-policy-7', 'FEE', 'DEBIT', 25, 'CRD'),
       ('insurance-claim-9', 'ADJUSTMENT', 'CREDIT', 20, 'CRD'),
       ('insurance-claim-10', 'INSURANCE_GUARANTEE', 'CREDIT', 15, 'CRD');

DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM insurance_ledger_invariant_snapshot;
  IF r.missing_premium_entries <> 0 OR r.missing_claim_entries <> 0
     OR r.malformed_premium_entries <> 0 OR r.malformed_claim_entries <> 0
     OR r.duplicate_premium_groups <> 0 OR r.duplicate_claim_groups <> 0 THEN
    RAISE EXCEPTION 'clean insurance ledger snapshot reported violations: %', r;
  END IF;

  INSERT INTO bank_transactions (tx_group_id, tx_type, direction, amount, currency)
  VALUES ('insurance-policy-7', 'INTEREST', 'CREDIT', 25, 'USD');
  SELECT * INTO r FROM insurance_ledger_invariant_snapshot;
  IF r.malformed_premium_entries <> 1 OR r.duplicate_premium_groups <> 1 THEN
    RAISE EXCEPTION 'malformed or duplicate premium was not visible: %', r;
  END IF;

  DELETE FROM bank_transactions WHERE tx_group_id = 'insurance-claim-9';
  SELECT * INTO r FROM insurance_ledger_invariant_snapshot;
  IF r.missing_claim_entries <> 1 THEN
    RAISE EXCEPTION 'missing claim settlement was not visible: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL insurance ledger invariant checks passed.'
