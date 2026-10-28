"""Static regression checks for cargo repository input and SQL safety."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CargoRepositoryGuardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "src/db/repo/repo_cargo.c").read_text()
        cls.ships = (ROOT / "src/db/repo/repo_ships.c").read_text()

    def test_every_sql_template_build_failure_is_checked(self):
        # An unchecked sql_build failure submits an empty statement. Keep every
        # call under an explicit return-status check, including legacy sync.
        self.assertNotRegex(self.source, r"(?m)^\s*sql_build\s*\(")
        self.assertGreaterEqual(self.source.count("sql_build (db,"), 10)

    def test_reads_and_writes_reject_unknown_commodity_codes(self):
        get_body = self.source.split("repo_cargo_get (", 1)[1].split(
            "repo_cargo_add (", 1
        )[0]
        add_body = self.source.split("repo_cargo_add (", 1)[1].split(
            "repo_cargo_sync_legacy_columns (", 1
        )[0]
        self.assertIn("is_valid_commodity_code (commodity_code)", get_body)
        self.assertIn("is_valid_commodity_code (commodity_code)", add_body)
        self.assertIn("return cargo_rc;", add_body)

    def test_capacity_arithmetic_rejects_signed_overflow(self):
        add_body = self.source.split("repo_cargo_add (", 1)[1].split(
            "repo_cargo_sync_legacy_columns (", 1
        )[0]
        self.assertIn("__builtin_add_overflow (current_qty, delta, &new_qty)", add_body)
        self.assertIn("__builtin_add_overflow (total_other, new_qty, &total_after)", add_body)

    def test_ship_repository_checks_sql_build_and_whitelists_cargo_columns(self):
        self.assertNotRegex(self.ships, r"(?m)^\s*sql_build\s*\(")
        body = self.ships.split("int repo_ships_update_cargo_column(", 1)[1]
        for column in ("ore", "organics", "equipment", "colonists", "slaves", "weapons", "drugs"):
            self.assertIn(f'"{column}"', body)
        self.assertIn("if (!allowed) return ERR_INVALID_ARG;", body)


if __name__ == "__main__":
    unittest.main()
