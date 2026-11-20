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
        persisted = "193_v3_photon_ship_disablement.sql"
        self.assertIn(persisted, manifest["fresh_install"]["v3_runtime"])
        self.assertTrue(any(entry.get("name") == persisted
                            for entry in manifest["scripts"]))

    def test_command_checks_adjacent_eligible_target_and_consumes_once(self):
        source = (ROOT / "src/server_combat.c").read_text()
        handler = source[source.index("cmd_combat_fire_photon ("):
                         source.index("cmd_combat_attack (")]
        for evidence in ("require_auth", "h_warp_exists", "Missile Frigate",
                         "Imperial Starship", "photons=photons-1",
                         "photon_fire_requests", "expires_at", "target_ship_id",
                         "combat_disabled_until", "direct_impact_applied"):
            self.assertIn(evidence, handler)

    def test_protocol_requires_explicit_target_ship(self):
        source = (ROOT / "src/schemas.c").read_text()
        start = source.index("schema_combat_fire_photon (void)", source.index("json_t *\nschema_combat_status (void)"))
        end = source.index("schema_fighters_recall (void)", start)
        schema = source[start:end]
        self.assertIn('json_string ("target_ship_id")', schema)

    def test_photon_disablement_guards_each_combat_initiation_path(self):
        combat = (ROOT / "src/server_combat.c").read_text()
        planets = (ROOT / "src/server_planets.c").read_text()
        ports = (ROOT / "src/server_ports.c").read_text()
        self.assertIn("refuse_photon_disabled_player (ctx, root, db, attacker_id)", combat)
        self.assertIn("Photon-disabled combat systems cannot initiate attacks or fire weapons", combat)
        self.assertIn("combat_disabled_until > (int64_t) time (NULL)", planets)
        self.assertIn("attacker.combat_disabled_until>(int64_t)time(NULL)", ports)
        self.assertIn("attacker_disabled_until > (int64_t)time (NULL)", combat)

    def test_persistence_migration_adds_ship_disablement_and_target_receipt(self):
        migration = (ROOT / "sql/pg/193_v3_photon_ship_disablement.sql").read_text()
        for field in ("combat_disabled_until", "target_ship_id",
                      "direct_impact_applied", "target_disabled_until"):
            self.assertIn(field, migration)

    def test_python_menu_exposes_photon_workflow(self):
        menus = json.loads((ROOT / "client/python_client/menus.json").read_text())
        options = menus["menus"]["DEPLOYMENT_MAIN"]["options"]
        photon = next(option for option in options if option["key"] == "x")
        self.assertEqual(photon["action"]["rpc"]["command"], "combat.fire_photon")
        self.assertIn("target_ship_id", photon["action"]["rpc"]["data"])
        presenter = (ROOT / "client/python_client/client.py").read_text()
        self.assertIn("target ship shields blocked direct impact", presenter)
        self.assertIn("target ship combat systems disabled until", presenter)


if __name__ == "__main__":
    unittest.main()
