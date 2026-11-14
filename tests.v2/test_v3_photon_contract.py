import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class V3PhotonContractTests(unittest.TestCase):
    def test_migration_persists_effect_and_idempotency_records(self):
        migration = (ROOT / "sql/pg/183_v3_photon_effects.sql").read_text()
        self.assertIn("CREATE TABLE IF NOT EXISTS sector_photon_effects", migration)
        self.assertIn("CREATE TABLE IF NOT EXISTS photon_fire_requests", migration)
        self.assertIn("expires_at bigint NOT NULL", migration)
        manifest = json.loads((ROOT / "sql/pg/migration_manifest.json").read_text())
        name = "183_v3_photon_effects.sql"
        self.assertIn(name, manifest["fresh_install"]["v3_runtime"])

    def test_command_checks_adjacent_eligible_target_and_consumes_once(self):
        source = (ROOT / "src/server_combat.c").read_text()
        handler = source[source.index("cmd_combat_fire_photon ("):
                         source.index("cmd_combat_attack (")]
        for evidence in ("require_auth", "h_warp_exists", "Missile Frigate",
                         "Imperial Starship", "photons=photons-1",
                         "photon_fire_requests", "expires_at"):
            self.assertIn(evidence, handler)

    def test_python_menu_exposes_photon_workflow(self):
        menus = json.loads((ROOT / "client/python_client/menus.json").read_text())
        options = menus["menus"]["DEPLOYMENT_MAIN"]["options"]
        photon = next(option for option in options if option["key"] == "x")
        self.assertEqual(photon["action"]["rpc"]["command"], "combat.fire_photon")


if __name__ == "__main__":
    unittest.main()
