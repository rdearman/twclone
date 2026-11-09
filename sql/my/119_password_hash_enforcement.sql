-- Run tools/migrate_passwords_argon2id_mysql.py while the application is
-- stopped before applying this migration to an existing database.
-- MySQL 8.0.16+ and MariaDB 10.2.1+ enforce CHECK constraints.

ALTER TABLE players
    MODIFY passwd VARCHAR(255) CHARACTER SET ascii COLLATE ascii_bin NOT NULL;

SET @twclone_password_check_exists = (
    SELECT COUNT(*)
    FROM information_schema.table_constraints
    WHERE constraint_schema = DATABASE()
      AND table_name = 'players'
      AND constraint_name = 'players_passwd_argon2id_check'
      AND constraint_type = 'CHECK'
);

SET @twclone_password_check_ddl = IF(
    @twclone_password_check_exists = 0,
    'ALTER TABLE players ADD CONSTRAINT players_passwd_argon2id_check CHECK (passwd REGEXP ''^[$]argon2id[$]v=19[$]m=[0-9]+,t=[0-9]+,p=[0-9]+[$][A-Za-z0-9+/]+[$][A-Za-z0-9+/]+$'')',
    'SELECT 1'
);
PREPARE twclone_password_check_stmt FROM @twclone_password_check_ddl;
EXECUTE twclone_password_check_stmt;
DEALLOCATE PREPARE twclone_password_check_stmt;
