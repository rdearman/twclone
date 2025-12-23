-- 090_other.sql
-- Converted from SQLite inline triggers to PostgreSQL Functions + Triggers

-- 1. Ship Shield Initialization
-- Optimization: Switched to BEFORE INSERT to modify NEW directly instead of running a second UPDATE
CREATE OR REPLACE FUNCTION fn_ships_ai_set_installed_shields() RETURNS TRIGGER AS $$
BEGIN
    NEW.installed_shields := LEAST(
        NEW.shields, 
        COALESCE(
            (SELECT maxshields FROM shiptypes st WHERE st.id = NEW.type_id), 
            NEW.shields
        )
    );
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER ships_ai_set_installed_shields
BEFORE INSERT ON ships
FOR EACH ROW
EXECUTE FUNCTION fn_ships_ai_set_installed_shields();


-- 2. Planet Cap Check
CREATE OR REPLACE FUNCTION fn_planets_total_cap_before_insert() RETURNS TRIGGER AS $$
DECLARE
    current_count BIGINT;
    max_allowed BIGINT;
BEGIN
    -- Allow System (0) to bypass cap during generation
    IF NEW.created_by = 0 THEN
        RETURN NEW;
    END IF;

    SELECT count(*) INTO current_count FROM planets;
    SELECT CAST(value AS BIGINT) INTO max_allowed FROM config WHERE key = 'max_total_planets';
    
    IF current_count >= max_allowed THEN
        RAISE EXCEPTION 'ERR_UNIVERSE_FULL';
    END IF;
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_planets_total_cap_before_insert
BEFORE INSERT ON planets
FOR EACH ROW
EXECUTE FUNCTION fn_planets_total_cap_before_insert();


-- 3. Corporation Updated Timestamp
CREATE OR REPLACE FUNCTION fn_corporations_touch_updated() RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at := CURRENT_TIMESTAMP;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER corporations_touch_updated
BEFORE UPDATE ON corporations
FOR EACH ROW
EXECUTE FUNCTION fn_corporations_touch_updated();


-- 4. Corp Owner Must Be Member (Auto-Insert)
CREATE OR REPLACE FUNCTION fn_corp_owner_must_be_member_insert() RETURNS TRIGGER AS $$
BEGIN
    IF NEW.owner_id IS NOT NULL AND NOT EXISTS (SELECT 1 FROM corp_members WHERE corp_id=NEW.id AND player_id=NEW.owner_id) THEN
        INSERT INTO corp_members(corp_id, player_id, role) VALUES(NEW.id, NEW.owner_id, 'Leader');
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER corp_owner_must_be_member_insert
AFTER INSERT ON corporations
FOR EACH ROW
EXECUTE FUNCTION fn_corp_owner_must_be_member_insert();


-- 5. Corp One Leader Guard
CREATE OR REPLACE FUNCTION fn_corp_one_leader_guard() RETURNS TRIGGER AS $$
BEGIN
    IF NEW.role = 'Leader' AND EXISTS (SELECT 1 FROM corp_members WHERE corp_id=NEW.corp_id AND role='Leader') THEN
        RAISE EXCEPTION 'corp may have only one Leader';
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER corp_one_leader_guard
BEFORE INSERT ON corp_members
FOR EACH ROW
EXECUTE FUNCTION fn_corp_one_leader_guard();


-- 6. Corp Owner/Leader Sync
CREATE OR REPLACE FUNCTION fn_corp_owner_leader_sync() RETURNS TRIGGER AS $$
BEGIN
    -- Downgrade old leader
    UPDATE corp_members SET role='Officer' 
    WHERE corp_id=NEW.id AND role='Leader' AND player_id <> NEW.owner_id;

    -- Ensure new owner is leader
    INSERT INTO corp_members(corp_id, player_id, role) 
    VALUES(NEW.id, NEW.owner_id, 'Leader') 
    ON CONFLICT(corp_id, player_id) DO UPDATE SET role='Leader';
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER corp_owner_leader_sync
AFTER UPDATE OF owner_id ON corporations
FOR EACH ROW
EXECUTE FUNCTION fn_corp_owner_leader_sync();


-- 7. Bank Transactions: Insufficient Funds Check
CREATE OR REPLACE FUNCTION fn_bank_transactions_before_insert() RETURNS TRIGGER AS $$
DECLARE
    current_bal BIGINT;
BEGIN
    IF NEW.direction = 'DEBIT' THEN
        SELECT balance INTO current_bal FROM bank_accounts WHERE id = NEW.account_id;
        IF (current_bal - NEW.amount) < 0 THEN
            RAISE EXCEPTION 'BANK_INSUFFICIENT_FUNDS';
        END IF;
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_bank_transactions_before_insert
BEFORE INSERT ON bank_transactions
FOR EACH ROW
EXECUTE FUNCTION fn_bank_transactions_before_insert();


-- 8. Bank Transactions: Update Balance
CREATE OR REPLACE FUNCTION fn_bank_transactions_after_insert() RETURNS TRIGGER AS $$
BEGIN
    UPDATE bank_accounts 
    SET balance = CASE NEW.direction 
        WHEN 'DEBIT' THEN balance - NEW.amount 
        WHEN 'CREDIT' THEN balance + NEW.amount 
        ELSE balance 
    END 
    WHERE id = NEW.account_id;

    -- Update the transaction record with the resulting balance (requires another update in PG unless logic is moved to app layer, keeping as is to match legacy logic)
    UPDATE bank_transactions 
    SET balance_after = (SELECT balance FROM bank_accounts WHERE id = NEW.account_id) 
    WHERE id = NEW.id;
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_bank_transactions_after_insert
AFTER INSERT ON bank_transactions
FOR EACH ROW
EXECUTE FUNCTION fn_bank_transactions_after_insert();


-- 9. Bank Transactions: Prevent Deletion
CREATE OR REPLACE FUNCTION fn_bank_transactions_before_delete() RETURNS TRIGGER AS $$
BEGIN
    RAISE EXCEPTION 'BANK_LEDGER_APPEND_ONLY';
    RETURN OLD;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_bank_transactions_before_delete
BEFORE DELETE ON bank_transactions
FOR EACH ROW
EXECUTE FUNCTION fn_bank_transactions_before_delete();


-- 10. Corp Transactions: Before Insert (Create account, check funds)
CREATE OR REPLACE FUNCTION fn_corp_tx_before_insert() RETURNS TRIGGER AS $$
DECLARE
    current_bal BIGINT;
BEGIN
    -- Ensure account exists
    INSERT INTO corp_accounts(corp_id, currency, balance, last_interest_at) 
    VALUES (NEW.corp_id, COALESCE(NEW.currency,'CRD'), 0, NULL)
    ON CONFLICT DO NOTHING;

    -- Check funds for outgoing types
    IF NEW.kind IN ('withdraw','transfer_out','dividend','salary') THEN
        SELECT balance INTO current_bal FROM corp_accounts WHERE corp_id = NEW.corp_id;
        IF (current_bal - NEW.amount) < 0 THEN
            RAISE EXCEPTION 'CORP_INSUFFICIENT_FUNDS';
        END IF;
    END IF;
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_corp_tx_before_insert
BEFORE INSERT ON corp_tx
FOR EACH ROW
EXECUTE FUNCTION fn_corp_tx_before_insert();


-- 11. Corp Transactions: After Insert (Update balance)
CREATE OR REPLACE FUNCTION fn_corp_tx_after_insert() RETURNS TRIGGER AS $$
BEGIN
    UPDATE corp_accounts 
    SET balance = CASE 
        WHEN NEW.kind IN ('withdraw', 'transfer_out', 'dividend', 'salary') THEN balance - NEW.amount 
        ELSE balance + NEW.amount 
    END 
    WHERE corp_id = NEW.corp_id;

    UPDATE corp_tx 
    SET balance_after = (SELECT balance FROM corp_accounts WHERE corp_id = NEW.corp_id) 
    WHERE id = NEW.id;
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_corp_tx_after_insert
AFTER INSERT ON corp_tx
FOR EACH ROW
EXECUTE FUNCTION fn_corp_tx_after_insert();
