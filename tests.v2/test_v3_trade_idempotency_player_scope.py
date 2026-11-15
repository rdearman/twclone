import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class TradeIdempotencyPlayerScopeTests(unittest.TestCase):
    def test_fresh_postgresql_and_mysql_schemas_scope_keys_to_players(self):
        pg = (ROOT / "sql/pg/000_tables.sql").read_text(encoding="utf-8")
        mysql = (ROOT / "sql/my/000_tables.sql").read_text(encoding="utf-8")
        pg_table = pg.split("CREATE TABLE trade_idempotency (", 1)[1].split("\n);", 1)[0]
        mysql_table = mysql.split("CREATE TABLE trade_idempotency (", 1)[1].split("\nCREATE TABLE", 1)[0]
        self.assertIn("PRIMARY KEY (key, player_id)", pg_table)
        self.assertIn("PRIMARY KEY (`key`, player_id)", mysql_table)
        self.assertNotIn("key TEXT PRIMARY KEY", pg_table)
        self.assertNotIn("COLLATE utf8mb4_bin PRIMARY KEY", mysql_table)

    def test_upgrade_migration_is_required_and_scoped(self):
        pg = (ROOT / "sql/pg/189_trade_idempotency_player_scope.sql").read_text()
        mysql = (ROOT / "sql/my/189_trade_idempotency_player_scope.sql").read_text()
        manifest = json.loads((ROOT / "sql/pg/migration_manifest.json").read_text())
        entry = next(item for item in manifest["scripts"]
                     if item["name"] == "189_trade_idempotency_player_scope.sql")
        self.assertTrue(entry["required"])
        self.assertFalse(entry["v3_only"])
        self.assertIn("PRIMARY KEY (key, player_id)", pg)
        self.assertIn("PRIMARY KEY (`key`, player_id)", mysql)

    def test_repository_lookup_and_documentation_are_player_scoped(self):
        repo = (ROOT / "src/db/repo/repo_ports.c").read_text()
        lookup = repo[repo.index("int db_ports_lookup_idemp("):
                      repo.index("int db_ports_check_port_id(")]
        self.assertIn("WHERE %s = {1} AND player_id = {2}", lookup)
        self.assertIn("Idempotency keys are player-scoped", lookup)


if __name__ == "__main__":
    unittest.main()
