"""Static checks for MySQL migration portability (no database connection)."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
MYSQL_MIGRATIONS = ROOT / "sql" / "my"
MYSQL_SCHEMA = MYSQL_MIGRATIONS / "000_tables.sql"
PG_SCHEMA = ROOT / "sql" / "pg" / "000_tables.sql"


def table_blocks(sql):
    """Return CREATE TABLE bodies, balancing parentheses outside SQL strings."""
    starts = re.finditer(r"(?im)^CREATE TABLE\s+`?(\w+)`?\s*\(", sql)
    for match in starts:
        open_pos = sql.find("(", match.start())
        depth = 0
        quote = None
        i = open_pos
        while i < len(sql):
            char = sql[i]
            nxt = sql[i + 1] if i + 1 < len(sql) else ""
            if quote:
                if quote == "`" and char == "`":
                    if nxt == "`":
                        i += 1
                    else:
                        quote = None
                elif quote != "`" and char == "\\":
                    i += 1
                elif char == quote:
                    if nxt == quote:
                        i += 1
                    else:
                        quote = None
            elif char in "'\"`":
                quote = char
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    yield match.group(1), sql[open_pos + 1:i]
                    break
            i += 1


class MysqlMigrationPortabilityTests(unittest.TestCase):
    def test_v2_migrations_do_not_use_postgres_index_if_not_exists(self):
        for migration in sorted(MYSQL_MIGRATIONS.glob("09[89]_*.sql")) + sorted(MYSQL_MIGRATIONS.glob("10[0-8]_*.sql")):
            with self.subTest(migration=migration.name):
                sql = migration.read_text(encoding="utf-8")
                sql = re.sub(r"(?m)^\s*--.*$", "", sql)
                self.assertNotRegex(sql, r"(?i)CREATE\s+(?:UNIQUE\s+)?INDEX\s+IF\s+NOT\s+EXISTS")

    def test_port_behaviour_foreign_key_widths_match_parent_keys(self):
        sql = (MYSQL_MIGRATIONS / "104_phase8_port_behaviour.sql").read_text(encoding="utf-8")
        self.assertEqual(len(re.findall(r"porttype_id\s+BIGINT\s+NOT NULL", sql, re.I)), 3)
        self.assertRegex(sql, r"cluster_id\s+BIGINT\s+NOT NULL")

    def test_session_timestamp_conversion_keeps_epoch_source_until_validated(self):
        sql = (MYSQL_MIGRATIONS / "098_migrate_sessions_expires_to_timestamp.sql").read_text(encoding="utf-8")
        self.assertIn("expires_converted TIMESTAMP NULL", sql)
        self.assertLess(sql.index("IF invalid_rows > 0"), sql.index("ALTER TABLE sessions DROP COLUMN expires"))
        self.assertIn("FROM_UNIXTIME(expires)", sql)

    def test_base_schema_keys_are_indexable_and_references_explicit(self):
        my_sql = MYSQL_SCHEMA.read_text(encoding="utf-8")
        pg_sql = PG_SCHEMA.read_text(encoding="utf-8")
        my_tables = dict(table_blocks(my_sql))
        pg_tables = dict(table_blocks(pg_sql))
        self.assertEqual(set(my_tables), set(pg_tables))

        for table, body in my_tables.items():
            with self.subTest(table=table):
                for line in body.splitlines():
                    if re.search(r"\bREFERENCES\b", line, re.I):
                        self.assertRegex(line, r"(?i)FOREIGN\s+KEY")
                text_columns = []
                for line in body.splitlines():
                    col = re.match(r"\s*`?(\w+)`?\s+TEXT\b(.*)", line, re.I)
                    if col:
                        text_columns.append((col.group(1), col.group(2), line))
                for name, tail, line in text_columns:
                    used = bool(re.search(r"\b(PRIMARY\s+KEY|UNIQUE|KEY|INDEX|REFERENCES)\b", tail, re.I))
                    used |= any(
                        re.search(r"\b(PRIMARY\s+KEY|UNIQUE(?:\s+KEY)?|KEY|INDEX|FOREIGN\s+KEY)\b", other, re.I)
                        and re.search(r"(?<!\w)" + re.escape(name) + r"(?!\w)", other, re.I)
                        for other in body.splitlines()
                    )
                    self.assertFalse(used, f"indexed TEXT key remains: {table}.{name}: {line.strip()}")

    def test_secondary_indexes_avoid_text_prefix_keys(self):
        indexes = (MYSQL_MIGRATIONS / "010_indexes.sql").read_text(encoding="utf-8")
        # Functional corporation indexes are deliberate expressions. Every
        # ordinary index now uses the complete, bounded key column.
        indexed_columns = (
            "cmd|name|idempotency_key|role|event_type|scope|status|owner_type|currency|"
            "tx_type|holder_type|idem_key|tag"
        )
        self.assertNotRegex(indexes, rf"(?i)\b(?:{indexed_columns})\s*\(\s*\d+\s*\)")
        schema = MYSQL_SCHEMA.read_text(encoding="utf-8")
        for table, column, size in (
            ("idempotency", "cmd", 191),
            ("players", "name", 191),
            ("corporations", "name", 191),
            ("corporations", "tag", 16),
            ("corp_members", "role", 32),
            ("corp_log", "event_type", 128),
            ("mail", "idempotency_key", 191),
            ("system_events", "scope", 64),
            ("commodity_orders", "status", 16),
            ("bank_accounts", "owner_type", 32),
            ("bank_transactions", "idempotency_key", 191),
            ("bank_fee_schedules", "tx_type", 64),
            ("bank_fee_schedules", "owner_type", 32),
            ("bank_fee_schedules", "currency", 16),
            ("stock_orders", "status", 16),
            ("insurance_policies", "holder_type", 32),
            ("engine_events", "idem_key", 191),
            ("engine_commands", "idem_key", 191),
            ("engine_commands", "status", 32),
        ):
            with self.subTest(table=table, column=column):
                body = dict(table_blocks(schema))[table]
                self.assertRegex(
                    body,
                    rf"(?im)^\s*`?{re.escape(column)}`?\s+VARCHAR\({size}\)\s+CHARACTER SET utf8mb4 COLLATE utf8mb4_bin\b",
                )
        portability = (MYSQL_MIGRATIONS / "099_mysql_key_portability.sql").read_text(encoding="utf-8")
        self.assertIn("SELECT 'players', 'name', 191", portability)
        self.assertIn("SELECT 'bank_fee_schedules', 'tx_type', 64", portability)
        self.assertIn("SELECT 'engine_commands', 'status', 32", portability)

    def test_mysql_schema_repairs_migration_dependencies(self):
        schema = MYSQL_SCHEMA.read_text(encoding="utf-8")
        self.assertIn("CREATE TABLE chat (", schema)
        self.assertIn("CREATE TABLE s2s_peers (", schema)
        self.assertIn("CREATE TABLE s2s_nonce_seen (", schema)
        portability = (MYSQL_MIGRATIONS / "099_mysql_key_portability.sql").read_text(encoding="utf-8")
        self.assertIn("CHAR_LENGTH(`", portability)
        self.assertIn("migrate_mysql_foreign_keys_099", portability)
        self.assertIn("CREATE TABLE IF NOT EXISTS ship_cargo", portability)
        cargo = (MYSQL_MIGRATIONS / "100_init_ship_cargo.sql").read_text(encoding="utf-8")
        self.assertIn("CREATE TABLE IF NOT EXISTS ship_cargo", cargo)
        behaviour = (MYSQL_MIGRATIONS / "104_phase8_port_behaviour.sql").read_text(encoding="utf-8")
        self.assertEqual(len(re.findall(r"porttype_id\s+BIGINT\s+NOT NULL", behaviour, re.I)), 3)
        self.assertIn("commodity_code VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin", behaviour)
        pricing = (MYSQL_MIGRATIONS / "105_phase9_dynamic_pricing.sql").read_text(encoding="utf-8")
        self.assertRegex(pricing, r"port_id\s+BIGINT\s+NOT NULL")
        self.assertIn("commodity_code VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin", pricing)

    def test_mysql_base_schema_avoids_postgres_and_mysql_ddl_traps(self):
        schema = MYSQL_SCHEMA.read_text(encoding="utf-8")
        self.assertNotRegex(schema, r"(?i)\bBIGSERIAL\b")
        self.assertNotRegex(schema, r"(?m)^[^-\n]*~\s*'[^']*'")
        self.assertNotRegex(
            schema,
            r"(?i)\bTEXT\s+(?:NOT\s+NULL\s+)?DEFAULT\s+(?!\()",
        )
        self.assertNotRegex(
            schema,
            r"(?i)\bAUTO_INCREMENT\s+PRIMARY\s+KEY\s+CHECK\s*\(\s*\w+\s*=",
        )
        self.assertNotRegex(schema, r"(?m)^[-=]{5,}")

    def test_mysql_gameplay_seed_uses_replayable_mysql_triggers(self):
        sql = (MYSQL_MIGRATIONS / "092_seed_gameplay.sql").read_text(encoding="utf-8")
        self.assertNotRegex(sql, r"(?i)CREATE\s+OR\s+REPLACE\s+FUNCTION|LANGUAGE\s+plpgsql")
        for trigger in (
            "ships_ai_set_installed_shields",
            "trg_planets_total_cap_before_insert",
            "corporations_touch_updated",
            "corp_owner_must_be_member_insert",
            "corp_one_leader_guard",
            "corp_owner_leader_sync",
            "trg_bank_transactions_before_insert",
            "trg_bank_transactions_after_insert",
            "trg_bank_transactions_before_delete",
            "trg_corp_tx_before_insert",
            "trg_corp_tx_after_insert",
        ):
            with self.subTest(trigger=trigger):
                self.assertIn(f"DROP TRIGGER IF EXISTS {trigger}", sql)
                self.assertRegex(sql, rf"(?i)CREATE\s+TRIGGER\s+{trigger}\b")
        for message in (
            "ERR_UNIVERSE_FULL",
            "BANK_INSUFFICIENT_FUNDS",
            "BANK_LEDGER_APPEND_ONLY",
            "CORP_INSUFFICIENT_FUNDS",
        ):
            self.assertIn(message, sql)

    def test_key_migration_preflights_orphans_before_dropping_foreign_keys(self):
        portability = (MYSQL_MIGRATIONS / "099_mysql_key_portability.sql").read_text(encoding="utf-8")
        preflight = portability.index("CALL migrate_mysql_foreign_keys_099(FALSE)")
        key_conversion = portability.index("CALL migrate_mysql_keys_099()")
        restore = portability.index("CALL migrate_mysql_foreign_keys_099(TRUE)")
        self.assertLess(preflight, key_conversion)
        self.assertLess(key_conversion, restore)
        self.assertIn("IF NOT add_constraints THEN", portability)
        self.assertIn("CAST(c.`", portability)
        self.assertIn("AS BINARY) = CAST(p.`", portability)
        self.assertIn("key value exceeds target VARCHAR width", portability)
        self.assertIn("orphan values found; no foreign keys added", portability)

    def test_base_foreign_key_types_match_and_are_not_inline(self):
        tables = dict(table_blocks(MYSQL_SCHEMA.read_text(encoding="utf-8")))
        columns = {}
        fk_count = 0
        for table, body in tables.items():
            for line in body.splitlines():
                col = re.match(r"\s*`?(\w+)`?\s+([A-Z]+(?:\s*\([^)]*\))?(?:\s+UNSIGNED)?)\b(.*)", line, re.I)
                if col and col.group(1).upper() not in {"PRIMARY", "UNIQUE", "KEY", "INDEX", "FOREIGN", "CONSTRAINT", "CHECK"}:
                    columns[table.lower(), col.group(1).lower()] = (re.sub(r"\s+", "", col.group(2).lower()), col.group(3))

        for table, body in tables.items():
            for line in body.splitlines():
                fk = re.search(r"FOREIGN KEY\s*\(\s*`?(\w+)`?\s*\)\s*REFERENCES\s+`?(\w+)`?\s*\(\s*`?(\w+)", line, re.I)
                if not fk:
                    continue
                fk_count += 1
                child, parent_table, parent = fk.groups()
                child_type = columns[table.lower(), child.lower()]
                parent_type = columns[parent_table.lower(), parent.lower()]
                self.assertEqual(child_type[0], parent_type[0], f"{table}.{child} -> {parent_table}.{parent}")
                for attribute in ("CHARACTER SET", "COLLATE"):
                    child_match = re.search(attribute + r"\s+(\w+)", child_type[1], re.I)
                    parent_match = re.search(attribute + r"\s+(\w+)", parent_type[1], re.I)
                    if child_match or parent_match:
                        self.assertIsNotNone(child_match, f"{table}.{child} missing {attribute}")
                        self.assertIsNotNone(parent_match, f"{parent_table}.{parent} missing {attribute}")
                        self.assertEqual(child_match.group(1).lower(), parent_match.group(1).lower())
        self.assertGreaterEqual(fk_count, 125)


if __name__ == "__main__":
    unittest.main()
