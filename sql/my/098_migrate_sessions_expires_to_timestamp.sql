-- Convert legacy epoch-second sessions.expires values to TIMESTAMP.
-- MySQL TIMESTAMP ends in 2038; out-of-range epochs abort with expires intact.
-- DDL implicitly commits in MySQL, so the staging column makes interruption
-- recovery safe: expires remains intact until all values have been converted.
DROP PROCEDURE IF EXISTS migrate_sessions_expires_098;
DELIMITER $$
CREATE PROCEDURE migrate_sessions_expires_098()
BEGIN
  DECLARE expires_type VARCHAR(64) DEFAULT NULL;
  DECLARE staging_type VARCHAR(64) DEFAULT NULL;
  DECLARE invalid_rows BIGINT DEFAULT 0;

  SELECT MAX(DATA_TYPE) INTO expires_type
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'sessions'
    AND COLUMN_NAME = 'expires';

  SELECT MAX(DATA_TYPE) INTO staging_type
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'sessions'
    AND COLUMN_NAME = 'expires_converted';

  IF expires_type IS NULL THEN
    IF staging_type = 'timestamp' THEN
      ALTER TABLE sessions
        CHANGE COLUMN expires_converted expires TIMESTAMP NOT NULL;
    ELSE
      SIGNAL SQLSTATE '45000'
        SET MESSAGE_TEXT = 'sessions.expires is missing and no converted staging column exists';
    END IF;
  ELSEIF expires_type IN ('tinyint', 'smallint', 'mediumint', 'int', 'bigint', 'decimal') THEN
    IF staging_type IS NULL THEN
      ALTER TABLE sessions ADD COLUMN expires_converted TIMESTAMP NULL;
    ELSEIF staging_type <> 'timestamp' THEN
      SIGNAL SQLSTATE '45000'
        SET MESSAGE_TEXT = 'sessions.expires_converted has an unexpected type';
    END IF;

    UPDATE sessions
       SET expires_converted = FROM_UNIXTIME(expires)
     WHERE expires IS NOT NULL;

    SELECT COUNT(*) INTO invalid_rows
    FROM sessions
    WHERE expires IS NULL OR expires_converted IS NULL;
    IF invalid_rows > 0 THEN
      SIGNAL SQLSTATE '45000'
        SET MESSAGE_TEXT = 'session epoch values contain NULL or exceed TIMESTAMP range; source column retained';
    END IF;

    ALTER TABLE sessions DROP COLUMN expires;
    ALTER TABLE sessions
      CHANGE COLUMN expires_converted expires TIMESTAMP NOT NULL;
  ELSEIF expires_type = 'timestamp' THEN
    IF staging_type IS NOT NULL THEN
      SIGNAL SQLSTATE '45000'
        SET MESSAGE_TEXT = 'unexpected sessions.expires_converted column; inspect interrupted migration';
    END IF;
    -- Fresh installs and already-migrated databases need no conversion.
  ELSE
    SIGNAL SQLSTATE '45000'
      SET MESSAGE_TEXT = 'unsupported sessions.expires type; expected epoch integer or TIMESTAMP';
  END IF;
END$$
DELIMITER ;

CALL migrate_sessions_expires_098();
DROP PROCEDURE migrate_sessions_expires_098;
