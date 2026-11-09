#!/usr/bin/env python3
"""Unittest-only contract checks for the CODEX 4 V3-SHIP-02 QA slice.

These checks do not start a server, provision a database, or treat source
inspection as gameplay acceptance.  They make the next QA slice reproducible
when the disposable socket/database gate is restored.
"""

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "ai_player" / "scenarios" / "v3_codex4_ship02_ownership_invariants.json"
SHIP_SOURCE = ROOT / "src" / "server_ships.c"


class Issue490Ship02ContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
        cls.source = SHIP_SOURCE.read_text(encoding="utf-8")

    def test_slice_is_next_unblocked_ordinary_player_readback_only(self):
        self.assertEqual(self.catalog["requirement_id"], "V3-SHIP-02")
        self.assertEqual(self.catalog["execution_status"], "next-unblocked-live-transfer-qa")
        self.assertTrue(self.catalog["ai_policy"]["ordinary_player"])
        self.assertTrue(self.catalog["restart_required"])
        self.assertIn("no provisioning, migration, reset, or direct row writes", " ".join(
            self.catalog["evidence_contract"]["safety"]
        ))

    def test_cases_cover_roster_status_foreign_refusal_and_restart(self):
        cases = {case["id"]: case for case in self.catalog["cases"]}
        self.assertEqual(
            {
                "owner-roster", "selected-owned-status", "foreign-inspect-refusal",
                "foreign-transfer-refusal", "authorized-inactive-transfer",
                "active-transfer-refusal", "restart-readback",
            },
            set(cases),
        )
        for case in cases.values():
            self.assertTrue(case["commands"])
            self.assertTrue(case["assertions"])
            for command in case["commands"]:
                self.assertIn(command["command"], {"ship.list", "ship.status", "ship.inspect", "ship.transfer"})
                self.assertNotIn(command["command"], {"ship.sell", "shipyard.sell"})
                self.assertNotIn("sql", command["data"])

    def test_current_handlers_expose_read_contracts_without_claiming_lifecycle_mutation(self):
        self.assertIn('FROM ships s JOIN ship_ownership o', self.source)
        self.assertIn('o.is_primary = {2}', self.source)
        self.assertIn('"active",', self.source)
        self.assertIn('"hull"', self.source)
        self.assertIn('json_integer (ship_state.hull)', self.source)
        self.assertIn('!json_is_integer (requested)', self.source)
        self.assertIn('repo_ships_transfer_primary', self.source)
        self.assertIn('repo_players_check_exists', self.source)
        self.assertIn('db_tx_begin (db, DB_TX_IMMEDIATE', self.source)
        self.assertIn('send_response_refused_steal (ctx, root, ERR_TARGET_INVALID', self.source)
        self.assertIn('ERR_NOT_IMPLEMENTED', self.source)

    def test_transfer_scenario_is_explicitly_inactive_only_and_sale_remains_gated(self):
        cases = {case["id"]: case for case in self.catalog["cases"]}
        self.assertIn("owned_inactive", json.dumps(cases["authorized-inactive-transfer"]))
        self.assertIn("active ship cannot be transferred", " ".join(cases["active-transfer-refusal"]["assertions"]))
        self.assertIn("sale remains excluded", self.catalog["dependency_note"])

    def test_evidence_contract_requires_response_events_snapshots_and_restart(self):
        required = self.catalog["canonical_requirements"]["evidence"]
        self.assertEqual(
            required,
            ["response", "events", "state_before_after", "database_before_after"],
        )
        self.assertTrue(self.catalog["canonical_requirements"]["one_primary_owner_per_ship"])
        self.assertTrue(self.catalog["canonical_requirements"]["active_ship_reference_is_consistent"])
        self.assertTrue(self.catalog["canonical_requirements"]["foreign_object_refusal_is_side_effect_free"])


if __name__ == "__main__":
    unittest.main()
