"""Static server guardrail for density-scan visibility.

This test validates source ordering only. It is not live server or
PostgreSQL gameplay evidence.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ServerDensityVisibilityContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server_source = (ROOT / "src/server_universe.c").read_text(encoding="utf-8")
        start = cls.server_source.index("void\ncmd_sector_scan_density")
        end = cls.server_source.index("int\nh_warp_exists", start)
        cls.body = cls.server_source[start:end]
        cls.schema_source = (ROOT / "src/schemas.c").read_text(encoding="utf-8")

    def test_density_scan_rejects_remote_sector_before_query(self):
        visibility = self.body.index("sector_is_current_or_adjacent")
        query = self.body.index("repo_universe_get_density_sector_list")
        self.assertLess(visibility, query)

    def test_density_scan_uses_forbidden_sector_error(self):
        visibility = self.body.index("sector_is_current_or_adjacent")
        refusal = self.body.index("ERR_FORBIDDEN_IN_SECTOR", visibility)
        self.assertLess(visibility, refusal)

    def test_density_schema_allows_positive_target_sector(self):
        start = self.schema_source.index("json_t *\nschema_sector_scan_density (void)")
        end = self.schema_source.index("json_t *\nschema_sector_scan (void)", start)
        schema = self.schema_source[start:end]
        self.assertIn('json_object_set_new (props, "sector_id", sector_id);', schema)
        self.assertIn('json_object_set_new (sector_id, "minimum", json_integer (1));', schema)

    def test_density_handler_checks_null_context_before_reading_it(self):
        self.assertLess(self.body.index("if (!ctx || !root)"),
                        self.body.index("ctx->sector_id"))


if __name__ == "__main__":
    unittest.main()
