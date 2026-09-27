-- MySQL gameplay triggers. Keep this file safe to replay by replacing each
-- trigger by name. The corresponding PostgreSQL functions are in sql/pg.
DELIMITER $$

DROP TRIGGER IF EXISTS ships_ai_set_installed_shields$$
CREATE TRIGGER ships_ai_set_installed_shields
BEFORE INSERT ON ships
FOR EACH ROW
BEGIN
    SET NEW.installed_shields = LEAST(
        NEW.shields,
        COALESCE((
            SELECT maxshields
            FROM shiptypes
            WHERE shiptypes_id = NEW.type_id
        ), NEW.shields)
    );
END$$

DROP TRIGGER IF EXISTS trg_planets_total_cap_before_insert$$
CREATE TRIGGER trg_planets_total_cap_before_insert
BEFORE INSERT ON planets
FOR EACH ROW
BEGIN
    DECLARE current_count BIGINT DEFAULT 0;
    DECLARE max_allowed BIGINT DEFAULT NULL;

    IF NEW.created_by <> 0 THEN
        SELECT COUNT(*) INTO current_count FROM planets;
        SELECT CAST(value AS SIGNED) INTO max_allowed
        FROM config WHERE `key` = 'max_total_planets';
        IF current_count >= max_allowed THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'ERR_UNIVERSE_FULL';
        END IF;
    END IF;
END$$

DROP TRIGGER IF EXISTS corporations_touch_updated$$
CREATE TRIGGER corporations_touch_updated
BEFORE UPDATE ON corporations
FOR EACH ROW
BEGIN
    SET NEW.updated_at = CURRENT_TIMESTAMP;
END$$

DROP TRIGGER IF EXISTS corp_owner_must_be_member_insert$$
CREATE TRIGGER corp_owner_must_be_member_insert
AFTER INSERT ON corporations
FOR EACH ROW
BEGIN
    IF NEW.owner_id IS NOT NULL THEN
        INSERT INTO corp_members (corporation_id, player_id, `role`)
        VALUES (NEW.corporation_id, NEW.owner_id, 'Leader');
    END IF;
END$$

DROP TRIGGER IF EXISTS corp_one_leader_guard$$
CREATE TRIGGER corp_one_leader_guard
BEFORE INSERT ON corp_members
FOR EACH ROW
BEGIN
    IF NEW.`role` = 'Leader' AND EXISTS (
        SELECT 1 FROM corp_members
        WHERE corporation_id = NEW.corporation_id AND `role` = 'Leader'
    ) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'corp may have only one Leader';
    END IF;
END$$

DROP TRIGGER IF EXISTS corp_owner_leader_sync$$
CREATE TRIGGER corp_owner_leader_sync
AFTER UPDATE ON corporations
FOR EACH ROW
BEGIN
    IF NOT (NEW.owner_id <=> OLD.owner_id) THEN
        UPDATE corp_members
        SET `role` = 'Officer'
        WHERE corporation_id = NEW.corporation_id
          AND `role` = 'Leader'
          AND player_id <> NEW.owner_id;

        IF NEW.owner_id IS NOT NULL AND NOT EXISTS (
            SELECT 1 FROM corp_members
            WHERE corporation_id = NEW.corporation_id
              AND player_id = NEW.owner_id AND `role` = 'Leader'
        ) THEN
            INSERT INTO corp_members (corporation_id, player_id, `role`)
            VALUES (NEW.corporation_id, NEW.owner_id, 'Leader')
            ON DUPLICATE KEY UPDATE `role` = 'Leader';
        END IF;
    END IF;
END$$

DROP TRIGGER IF EXISTS trg_bank_transactions_before_insert$$
CREATE TRIGGER trg_bank_transactions_before_insert
BEFORE INSERT ON bank_transactions
FOR EACH ROW
BEGIN
    DECLARE current_balance BIGINT DEFAULT NULL;

    SELECT balance INTO current_balance
    FROM bank_accounts WHERE id = NEW.account_id FOR UPDATE;
    IF NEW.direction = 'DEBIT' AND current_balance - NEW.amount < 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'BANK_INSUFFICIENT_FUNDS';
    END IF;
    SET NEW.balance_after = CASE NEW.direction
        WHEN 'DEBIT' THEN current_balance - NEW.amount
        WHEN 'CREDIT' THEN current_balance + NEW.amount
        ELSE current_balance
    END;
END$$

DROP TRIGGER IF EXISTS trg_bank_transactions_after_insert$$
CREATE TRIGGER trg_bank_transactions_after_insert
AFTER INSERT ON bank_transactions
FOR EACH ROW
BEGIN
    UPDATE bank_accounts SET balance = NEW.balance_after WHERE id = NEW.account_id;
END$$

DROP TRIGGER IF EXISTS trg_bank_transactions_before_delete$$
CREATE TRIGGER trg_bank_transactions_before_delete
BEFORE DELETE ON bank_transactions
FOR EACH ROW
BEGIN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'BANK_LEDGER_APPEND_ONLY';
END$$

DROP TRIGGER IF EXISTS trg_corp_tx_before_insert$$
CREATE TRIGGER trg_corp_tx_before_insert
BEFORE INSERT ON corp_tx
FOR EACH ROW
BEGIN
    DECLARE current_balance BIGINT DEFAULT NULL;

    INSERT INTO corp_accounts (corp_id, currency, balance, last_interest_at)
    VALUES (NEW.corp_id, COALESCE(NEW.currency, 'CRD'), 0, NULL)
    ON DUPLICATE KEY UPDATE corp_id = VALUES(corp_id);

    SELECT balance INTO current_balance
    FROM corp_accounts WHERE corp_id = NEW.corp_id FOR UPDATE;

    IF NEW.kind IN ('withdraw', 'transfer_out', 'dividend', 'salary')
       AND current_balance - NEW.amount < 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CORP_INSUFFICIENT_FUNDS';
    END IF;

    SET NEW.balance_after = CASE
        WHEN NEW.kind IN ('withdraw', 'transfer_out', 'dividend', 'salary')
            THEN current_balance - NEW.amount
        ELSE current_balance + NEW.amount
    END;
END$$

DROP TRIGGER IF EXISTS trg_corp_tx_after_insert$$
CREATE TRIGGER trg_corp_tx_after_insert
AFTER INSERT ON corp_tx
FOR EACH ROW
BEGIN
    UPDATE corp_accounts SET balance = NEW.balance_after WHERE corp_id = NEW.corp_id;
END$$

DELIMITER ;
