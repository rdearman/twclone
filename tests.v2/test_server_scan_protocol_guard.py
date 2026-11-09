"""Static protocol guard for malformed sector.scan dispatch.

This verifies source ordering only; it is not live server evidence.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ServerScanProtocolGuardTests(unittest.TestCase):
    def test_sector_scan_validates_context_and_request_before_database_access(self):
        source = (ROOT / "src/server_universe.c").read_text(encoding="utf-8")
        start = source.index("void\ncmd_sector_scan")
        end = source.index("void\ncmd_sector_scan_density", start)
        body = source[start:end]

        guard = body.index("if (!ctx || !root)")
        db_access = body.index("game_db_get_handle")
        self.assertLess(guard, db_access)


if __name__ == "__main__":
    unittest.main()
