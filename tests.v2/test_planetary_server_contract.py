import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PlanetaryServerContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.planets = (ROOT / "src/server_planets.c").read_text(encoding="utf-8")
        cls.combat = (ROOT / "src/server_combat.c").read_text(encoding="utf-8")
        cls.loop = (ROOT / "src/server_loop.c").read_text(encoding="utf-8")
        cls.universe = (ROOT / "src/server_universe.c").read_text(encoding="utf-8")
        cls.universe_repo = (ROOT / "src/db/repo/repo_universe.c").read_text(
            encoding="utf-8"
        )
        cls.protocol = (ROOT / "docs/PROTOCOL.v3/24_Planets_Outposts_Stations.md").read_text(
            encoding="utf-8"
        )
        cls.suite = json.loads(
            (ROOT / "tests.v2/suite_planet_fighter_transfers.json").read_text(
                encoding="utf-8"
            )
        )
        cls.landing_suite = json.loads(
            (ROOT / "tests.v2/suite_planet_landing_defense.json").read_text(
                encoding="utf-8"
            )
        )

    def test_fighter_transfer_is_registered_and_uses_atomic_repository(self):
        self.assertIn('"planet.fighters.transfer", cmd_planet_fighters_transfer', self.loop)
        handler = self.planets.split("cmd_planet_fighters_transfer", 1)[1].split(
            "cmd_planet_rename", 1
        )[0]
        self.assertIn("repo_planets_transfer_fighters", handler)
        self.assertNotIn("ERR_NOT_IMPLEMENTED", handler)
        self.assertIn('"ship_to_planet"', handler)
        self.assertIn('"planet_to_ship"', handler)
        self.assertIn("landed_planet != planet_id", handler)
        self.assertIn("You do not control this planet", handler)
        self.assertIn("repo_corp_check_member_role", handler)
        self.assertIn("revalidates ownership, landing, available", handler)

    def test_treasury_is_not_authorized_by_landing_alone(self):
        info = self.planets.split("cmd_planet_info", 1)[1].split(
            "cmd_planet_fighters_transfer", 1
        )[0]
        self.assertIn("bool treasury_authorized = false", info)
        self.assertIn("if (treasury_authorized && citadel_exists)", info)
        self.assertIn("db_planets_get_treasury", info)
        self.assertIn("repo_corp_check_member_role", info)

    def test_landing_defense_fires_before_denial_and_reports_persistence_failure(self):
        land = self.planets.split("cmd_planet_land", 1)[1].split(
            "cmd_planet_launch", 1
        )[0]
        self.assertLess(
            land.index("h_trigger_atmosphere_quasar"),
            land.index("if (!can_land)"),
        )
        self.assertIn("defense_rc < 0", land)
        self.assertIn("defense_rc == 2", land)
        self.assertIn("resolve_landing_fighter_defense", land)
        self.assertIn("send_response_error_data", land)
        self.assertIn('"combat.planet_landing_defense.v1"', self.planets)
        self.assertIn('"combat.hit.quasar"', self.combat)

    def test_landing_validates_before_turn_and_commits_turn_with_landing(self):
        land = self.planets.split("cmd_planet_land", 1)[1].split(
            "cmd_planet_launch", 1
        )[0]
        self.assertLess(land.index("if (player_sector != planet_sector)"),
                        land.index("h_consume_player_turn"))
        self.assertLess(land.index("if (!can_land)"),
                        land.index("h_consume_player_turn"))
        self.assertIn("db_tx_begin (db, DB_TX_IMMEDIATE", land)
        self.assertIn("db_tx_rollback (db, NULL)", land)
        self.assertIn("db_tx_commit (db, &txerr)", land)

    def test_citadel_lookup_does_not_treat_query_failure_as_no_defense(self):
        self.assertIn("load_landing_citadel_defenses", self.planets)
        self.assertIn("rc == ERR_DB_NOT_FOUND", self.planets)
        self.assertIn("db_planets_get_citadel_defenses_ex", self.planets)
        self.assertIn("db_planets_consume_citadel_shields", self.planets)
        self.assertNotIn("db_planets_update_citadel_shields (db, planet_id,\n                                                shields_after)", self.planets)
        self.assertIn("state_rc == ERR_DB_NOT_FOUND", self.combat)
        self.assertIn("if (state_rc != 0)", self.combat)

    def test_server_defense_and_planet_transwarp_checks_match_manual_level_gates(self):
        # The original TW2002 manual assigns combat control to L2, shields to
        # L5, interdictor operation to L6, and Planetary TransWarp to L4.
        land_defense = self.planets.split(
            "resolve_landing_fighter_defense", 1
        )[1].split("cmd_planet_land", 1)[0]
        self.assertIn("citadel_level >= 2", land_defense)
        self.assertIn("citadel_level >= 5", land_defense)

        planet_transwarp = self.planets.split("cmd_planet_transwarp", 1)[1].split(
            "cmd_planet_colonists_set", 1
        )[0]
        self.assertIn("if (level < 4)", planet_transwarp)

        self.assertIn("c.level >= 6", self.universe_repo)
        interdiction = self.universe.split("h_check_interdiction (", 1)[1].split(
            "\nint\n", 1
        )[0]
        self.assertIn("repo_universe_get_interdictors", interdiction)

    def test_protocol_and_integration_suite_cover_transfer_and_visibility(self):
        self.assertIn("repo_planets_transfer_fighters(", self.protocol)
        names = [test["name"] for test in self.suite["tests"]]
        self.assertIn("Transfer fighters from ship to planet", names)
        self.assertIn("Transfer fighters from planet to ship", names)
        self.assertIn("Landed visitor does not receive treasury balance", names)
        self.assertIn("Reject transfer exceeding available ship fighters", names)

    def test_landing_regressions_cover_authorized_hostile_unguarded_and_lethal(self):
        names = [test["name"] for test in self.landing_suite["tests"]]
        for required in (
            "Planet owner bypasses its armed Quasar defense",
            "Authorized corporation member bypasses armed defense",
            "Undefended outsider landing is refused without damage",
            "Quasar damage consumes shields before hull",
            "Planetary fighters engage outsider; access remains denied",
            "Lethal Quasar defense destroys attacking ship",
        ):
            self.assertIn(required, names)

    def test_landing_suite_covers_fedspace_refusal_and_non_fedspace_combat(self):
        names = [test["name"] for test in self.landing_suite["tests"]]
        self.assertIn("FedSpace refusal prevents planetary defense engagement", names)
        self.assertIn("Verify protected-sector attempt caused no damage or defense event", names)
        fixture = next(
            test for test in self.landing_suite["tests"]
            if test.get("name") == "Create owned and defended planets"
        )
        self.assertIn("977712,20,", fixture["data"]["sql"])
        self.assertIn("protected_sector", self.planets)
        self.assertIn("ERR_DB_NOT_FOUND", self.planets)


if __name__ == "__main__":
    unittest.main()
