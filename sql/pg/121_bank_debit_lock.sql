-- Apply to existing PostgreSQL databases after 092. Safe to replay.
CREATE OR REPLACE FUNCTION fn_bank_transactions_before_insert ()
    RETURNS TRIGGER
    AS $$
DECLARE
    current_bal bigint;
BEGIN
    IF NEW.direction = 'DEBIT' THEN
        SELECT balance INTO current_bal
          FROM bank_accounts
         WHERE bank_accounts_id = NEW.account_id
         FOR UPDATE;
        IF NOT FOUND THEN
            RAISE EXCEPTION 'BANK_ACCOUNT_NOT_FOUND';
        END IF;
        IF (current_bal - NEW.amount) < 0 THEN
            RAISE EXCEPTION 'BANK_INSUFFICIENT_FUNDS';
        END IF;
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;
