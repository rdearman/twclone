# V2 password hashing

## Storage and authentication contract

`players.passwd` stores an encoded Argon2id string. The string includes the
version, work parameters, salt, and digest. Application writes use 64 MiB of
memory, three passes, one lane, a 16-byte random salt, and a 32-byte digest.
The implementation calls the maintained system `libargon2` runtime; it does
not implement password cryptography itself.

No password length constraint or database length limit existed. The old game
login path copied stored values into a 256-byte buffer, which made longer
passwords unreliable. PostgreSQL and MySQL schemas use text storage. Password
reset is disabled. Password change verifies the current value and stores a
fresh Argon2id hash. Registration paths hash before calling `register_player`.
Authentication treats malformed/non-Argon2id values as a failed password;
there is no plaintext fallback. Hashes are not returned by the login-info
repository method or included in client responses or logs.

System and Federation Administrator seed accounts are regular login-capable
accounts; their seed passwords are converted like every other row. NPC seed
rows, including rows with an empty legacy password, are also converted. NPC
login remains denied before password verification, preserving existing
behavior.

## PostgreSQL migration order

The legacy conversion is operational because PostgreSQL has no built-in
Argon2id function. For an existing database:

1. Coordinate a full application stop and verify there are no application
   database sessions. Do not attempt a rolling deployment.
2. Take a fresh custom-format `pg_dump` backup. Verify its checksum and restore
   it to a separate database before continuing.
3. On the target database, run a dry run:

   ```sh
   DATABASE_URL='postgresql://...' python3 tools/migrate_passwords_argon2id.py --dry-run
   ```

   The connection string must be supplied through a protected environment or
   secret manager. Do not put it in shell history or a report.
4. Run the same command without `--dry-run`. It rejects other connected
   database sessions, locks the player rows in one transaction, converts every
   plaintext value (including System/NPC accounts), and commits atomically.
   Output contains counts only. If it fails, the transaction rolls back.
5. Apply `sql/pg/119_password_hash_enforcement.sql` with `psql -v ON_ERROR_STOP=1`.
   It verifies the encoded format and adds a CHECK constraint, idempotently.
6. Deploy the matching server binary and start the server once. Verify System
   authentication/S2S, a player login, a failed wrong-password login, a new
   registration, and a password change. Keep the server stopped until database
   migration and binary replacement are both complete.

For a fresh install, PostgreSQL seed account values in migration 095 are
already encoded with distinct salts. Install schema, procedures, and seeds in
the documented order, then apply migration 119 before the first server start.
The conversion utility is safe to run and reports those seeded accounts as
already hashed. Never start an old binary against the hash-only database.

## Rollback

There is no safe reverse SQL migration: the previous plaintext values cannot
be recovered from Argon2id hashes. If qualification fails before the new
server accepts writes, stop the candidate and restore the verified pre-upgrade
backup, then restore the old binary. If the candidate has accepted account
registration or password changes, restoring the backup loses those post-backup
changes; preserve the failed database and coordinate account recovery before
rollback. Do not remove the constraint or convert hashes back to plaintext.

The live database migration is a separate approved operation. This document
does not authorize applying it to `twclone`.

## MySQL and MariaDB storage

MySQL and MariaDB use the same encoded Argon2id string and verification
function as PostgreSQL. The fresh schema stores `players.passwd` as
`VARCHAR(255) CHARACTER SET ascii COLLATE ascii_bin`; seeded NPC passwords
are individually salted hashes. `sql/my/095_player_registration.sql` also
seeds the `System` and `Federation Administrator` bootstrap accounts with the
canonical PostgreSQL seed hashes, preserving their existing bootstrap
passwords without introducing new credentials. Registration and password
changes pass through the shared repository hashing code. There is no
password-reset path.

Migration 119 requires MySQL 8.0.16 or newer, or MariaDB 10.2.1 or newer, so
the hash-only `CHECK` constraint is enforced by the server. Existing MySQL
databases must use InnoDB for `players`. The converter requires
`mysql-connector-python`, `libargon2`, and a migration account able to inspect
`PROCESSLIST`; it refuses to run if another session is connected to the
selected database. The minimum versions follow the [MySQL CHECK constraint
documentation](https://dev.mysql.com/doc/refman/8.0/en/create-table-check-constraints.html)
and [MariaDB CHECK support notes](https://mariadb.com/kb/en/check-constraints-support/).

### Existing MySQL database deployment

1. Stop all game-server and engine processes. Confirm that no application
   sessions remain connected to the target database.
2. Make a fresh consistent backup, record its checksum, restore it to a
   separate database, and verify the restored account count and data before
   proceeding. For example:

   ```sh
   mysqldump --single-transaction --routines --triggers --events \
     "$MYSQL_DATABASE" > "$TWCLONE_BACKUP_FILE"
   sha256sum "$TWCLONE_BACKUP_FILE"
   mysql -e "CREATE DATABASE \`$TWCLONE_RESTORE_DATABASE\`"
   mysql "$TWCLONE_RESTORE_DATABASE" < "$TWCLONE_BACKUP_FILE"
   ```

   Keep credentials in a protected option file or secret manager; do not put
   them on the command line or in shell history.
3. Set `MYSQL_USER`, `MYSQL_PASSWORD`, and `MYSQL_DATABASE`, plus either
   `MYSQL_HOST`/`MYSQL_PORT` or `MYSQL_SOCKET`, through a protected
   environment. Run a dry run, then the conversion:

   ```sh
   python3 tools/migrate_passwords_argon2id_mysql.py --dry-run
   python3 tools/migrate_passwords_argon2id_mysql.py
   ```

   The converter locks player rows, hashes every plaintext value (including
   empty legacy NPC values), rejects malformed pre-existing Argon2id strings,
   and commits all row updates together. Its output contains counts only.
4. Apply migration 119 with the same protected client configuration while
   the application remains stopped, for example:

   ```sh
   mysql <database> < sql/my/119_password_hash_enforcement.sql
   ```

   The DDL changes the column type and adds the named hash-only check if it is
   absent. The migration may be replayed.
5. Deploy the matching server binary and start it once. Verify successful
   System and player login, rejected wrong-password login, new registration,
   password change, and rejected plaintext database writes before reopening
   normal service.

### MySQL rollback

Do not convert hashes back to plaintext. If the candidate has not accepted
new registrations or password changes, stop it and restore the verified
pre-upgrade backup, then restore the previous binary. After password writes
have occurred, a backup restore loses those writes; preserve the upgraded
database and coordinate account recovery rather than attempting a reverse
conversion. MySQL DDL implicitly commits, so restoring the verified backup is
the rollback procedure if migration 119 fails partway through.

## Dependencies and tests

The server needs the `libargon2` runtime library and dynamic loader (`dl`),
`pthread` is already a server dependency, and the migration utility needs
Python `psycopg2`. Regression commands:

```sh
tests.v2/run_password_hash_test.sh
TEST_DATABASE_URL='postgresql://.../twclone_password_test_<unique>' \
  python3 tests.v2/test_password_hash_migration.py
python3 tests.v2/test_mysql_password_hash_static.py
MYSQL_TEST_DATABASE='twclone_mysql_password_test_<unique>' \
  python3 tests.v2/test_mysql_password_hash_migration.py
```

The database test refuses a database whose name does not start with
`twclone_password_test_`; it creates and drops only a temporary schema.
The MySQL integration test requires a disposable database whose name starts
with `twclone_mysql_password_test_`; it creates and drops only its own
`players` fixture table. It is skipped when the connector or test connection
is unavailable.
