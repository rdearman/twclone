"""Live MySQL integration coverage; requires an explicitly disposable DB."""

import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

try:
    import mysql.connector
except ImportError:
    mysql = None
else:
    mysql = mysql.connector

from tools.argon2id_passwords import is_valid_encoded, load_argon2
from tools.migrate_passwords_argon2id_mysql import migrate_rows


class MySQLPasswordMigrationIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if mysql is None:
            raise unittest.SkipTest("mysql-connector-python is not installed")
        cls.database = os.environ.get("MYSQL_TEST_DATABASE", "")
        if not cls.database.startswith("twclone_mysql_password_test_"):
            raise unittest.SkipTest(
                "MYSQL_TEST_DATABASE must name a dedicated twclone_mysql_password_test_* database"
            )
        if not os.environ.get("MYSQL_TEST_USER") or not (
            os.environ.get("MYSQL_TEST_HOST") or os.environ.get("MYSQL_TEST_SOCKET")
        ):
            raise unittest.SkipTest("MYSQL_TEST_USER and MYSQL_TEST_HOST or MYSQL_TEST_SOCKET are required")
        cls.api = load_argon2()

    def setUp(self):
        options = {
            "user": os.environ["MYSQL_TEST_USER"],
            "password": os.environ.get("MYSQL_TEST_PASSWORD", ""),
            "database": self.database,
            "autocommit": False,
        }
        if os.environ.get("MYSQL_TEST_SOCKET"):
            options["unix_socket"] = os.environ["MYSQL_TEST_SOCKET"]
        else:
            options["host"] = os.environ["MYSQL_TEST_HOST"]
        if os.environ.get("MYSQL_TEST_PORT") and "unix_socket" not in options:
            options["port"] = int(os.environ["MYSQL_TEST_PORT"])
        self.conn = mysql.connect(**options)
        self.cur = self.conn.cursor()
        self.cur.execute(
            "SELECT COUNT(*) FROM information_schema.tables "
            "WHERE table_schema = DATABASE() AND table_name = 'players'"
        )
        if self.cur.fetchone()[0]:
            self.fail("disposable MySQL password test database already contains players")
        self.cur.execute(
            "CREATE TABLE players (player_id BIGINT PRIMARY KEY, passwd TEXT NOT NULL) ENGINE=InnoDB"
        )
        self.cur.executemany(
            "INSERT INTO players (player_id, passwd) VALUES (%s, %s)",
            [(1, "legacy-system"), (2, "legacy-player"), (3, "")],
        )
        self.conn.commit()

    def tearDown(self):
        try:
            self.cur.execute("DROP TABLE IF EXISTS players")
            self.conn.commit()
        finally:
            self.cur.close()
            self.conn.close()

    def _apply_hash_enforcement_migration(self):
        migration = (ROOT / "sql/my/119_password_hash_enforcement.sql").read_text()
        for statement in migration.split(";"):
            statement = statement.strip()
            if not statement:
                continue
            self.cur.execute(statement)
            # On replay the prepared statement runs SELECT 1. Drain that
            # result before issuing the next migration statement.
            if self.cur.with_rows:
                self.cur.fetchall()

    def test_conversion_rollback_replay_enforcement_and_rejection(self):
        self.conn.start_transaction()
        self.assertEqual(migrate_rows(self.cur, self.api), (3, 3, 0))
        self.cur.execute("SELECT passwd FROM players ORDER BY player_id")
        first_hashes = [row[0] for row in self.cur.fetchall()]
        self.assertTrue(all(is_valid_encoded(self.api, value) for value in first_hashes))
        self.assertEqual(len(set(first_hashes)), 3)

        # A dry-run style rollback restores all source credentials.
        self.conn.rollback()
        self.cur.execute("SELECT passwd FROM players ORDER BY player_id")
        self.assertEqual([row[0] for row in self.cur.fetchall()], ["legacy-system", "legacy-player", ""])

        self.cur.execute("INSERT INTO players VALUES (4, %s)", ("$argon2id$broken",))
        self.conn.commit()
        self.conn.start_transaction()
        with self.assertRaises(ValueError):
            migrate_rows(self.cur, self.api)
        self.conn.rollback()
        self.cur.execute("DELETE FROM players WHERE player_id = 4")
        self.conn.commit()

        self.conn.start_transaction()
        self.assertEqual(migrate_rows(self.cur, self.api), (3, 3, 0))
        self.conn.commit()
        self.conn.start_transaction()
        self.assertEqual(migrate_rows(self.cur, self.api), (3, 0, 3))
        self.conn.commit()

        self._apply_hash_enforcement_migration()
        self._apply_hash_enforcement_migration()

        with self.assertRaises(mysql.Error):
            self.cur.execute("INSERT INTO players VALUES (5, 'plaintext-rejected')")
        self.conn.rollback()


if __name__ == "__main__":
    unittest.main()
