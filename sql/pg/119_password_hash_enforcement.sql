-- Passwords are converted by tools/migrate_passwords_argon2id.py while the
-- application is stopped. This migration only enforces the hash-only contract.
DO $migration$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM players
        WHERE passwd !~ $regex$^\$argon2id\$v=19\$m=[0-9]+,t=[0-9]+,p=[0-9]+\$[A-Za-z0-9+/]+\$[A-Za-z0-9+/]+$$regex$
    ) THEN
        RAISE EXCEPTION 'players.passwd contains values that are not encoded Argon2id hashes';
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conrelid = 'players'::regclass
          AND conname = 'players_passwd_argon2id_check'
    ) THEN
        ALTER TABLE players
            ADD CONSTRAINT players_passwd_argon2id_check
            CHECK (passwd ~ $regex$^\$argon2id\$v=19\$m=[0-9]+,t=[0-9]+,p=[0-9]+\$[A-Za-z0-9+/]+\$[A-Za-z0-9+/]+$$regex$);
    END IF;
END
$migration$;
