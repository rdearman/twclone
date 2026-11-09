"""Offline contract checks for the CODEX 4 V3-SHIP-02 QA slice.

These tests validate the ordinary-player boundary and evidence contract only.
They do not provision a database, run migrations, or claim live acceptance.
"""

import json
from pathlib import Path
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from qa_harness import run_ordinary_player_scenario


ROOT = Path(__file__).parents[1]
CATALOG = ROOT / "scenarios" / "v3_codex4_ship02_ownership_invariants.json"


def load_catalog():
    return json.loads(CATALOG.read_text(encoding="utf-8"))


def test_ship02_catalog_is_ordinary_player_and_read_only():
    catalog = load_catalog()
    assert catalog["requirement_id"] == "V3-SHIP-02"
    assert catalog["execution_status"] == "next-unblocked-qa-preparation"
    assert catalog["simulation_fixture"] is False
    assert catalog["ai_policy"]["ordinary_player"] is True
    assert "canonical_requirements" not in catalog["ai_policy"]
    assert "no provisioning, migration, reset, or direct row writes" in catalog["evidence_contract"]["safety"]


def test_ship02_cases_cover_positive_refusal_and_restart_boundaries():
    catalog = load_catalog()
    cases = {case["id"]: case for case in catalog["cases"]}
    assert {"owner-roster", "selected-owned-status", "foreign-inspect-refusal", "restart-readback"} <= set(cases)
    assert cases["foreign-inspect-refusal"]["expected"] == "refusal"
    assert catalog["canonical_requirements"]["one_primary_owner_per_ship"] is True
    for case in catalog["cases"]:
        for command in case["commands"]:
            assert not command["command"].startswith(("admin.", "sys."))


def test_ship02_catalog_is_read_only_and_excludes_blocked_lifecycle_mutations():
    catalog = load_catalog()
    allowed = {"ship.list", "ship.status", "ship.inspect"}
    for case in catalog["cases"]:
        assert case["expected"] in {"success", "refusal"}
        for command in case["commands"]:
            assert command["command"] in allowed
            assert "sql" not in command["data"]
            assert "recipient" not in command["data"]
    # Sale/transfer belongs to V3-SHIP-01, which is still a server-side stub;
    # this QA slice must not imply that it has been exercised or accepted.
    assert all(
        command["command"] not in {"ship.sell", "ship.transfer", "shipyard.sell"}
        for case in catalog["cases"]
        for command in case["commands"]
    )


class OwnershipAdapter:
    def __init__(self):
        self.index = 0

    def read_state(self, label):
        return {"sector_id": 20, "turns_remaining": 10, "ship_id": 7, "active_ship_id": 7}

    def read_db(self, label):
        return {
            "ships": [{"ship_id": 7, "owner_id": 11, "primary_owner_count": 1, "active": True}],
            "player": {"ship_id": 7, "credits": 1000},
        }

    def send(self, command):
        self.index += 1
        if command["data"].get("ship_id") == "foreign_ship":
            return {"status": "refused", "error": {"code": 1403}, "session_token": "private"}
        return {"status": "ok", "type": "ship.readback", "data": {"ship_id": 7}, "session_token": "private"}

    def read_events(self):
        return [{"type": "ship.read", "ship_id": 7, "session_token": "private"}]

    def read_restart_state(self):
        return self.read_state("after_restart")

    def read_restart_db(self):
        return self.read_db("after_restart")


def test_ship02_owner_roster_requires_restart_readback(tmp_path):
    catalog = load_catalog()
    outcome = run_ordinary_player_scenario(
        OwnershipAdapter(),
        tmp_path,
        scenario_id="V3-SHIP-02",
        case_id="owner-roster",
        commands=[{"command": "ship.list", "data": {}}],
        expected="success",
        canonical_requirements={
            **catalog["canonical_requirements"],
            "restart_required": catalog["restart_required"],
            "database_must_be_unchanged": True,
        },
        ai_policy=catalog["ai_policy"],
    )
    assert outcome["passed"] is True
    assert outcome["restart_checked"] is True
    assert outcome["state_changed"] is False
    assert outcome["db_changed"] is False


def test_ship02_runner_records_redacted_ordinary_player_evidence(tmp_path):
    outcome = run_ordinary_player_scenario(
        OwnershipAdapter(),
        tmp_path,
        scenario_id="V3-SHIP-02",
        case_id="selected-owned-status",
        commands=[
            {"command": "ship.status", "data": {"ship_id": "owned_active"}},
            {"command": "ship.inspect", "data": {"ship_id": "owned_active"}},
        ],
        expected="success",
        canonical_requirements=load_catalog()["canonical_requirements"],
        ai_policy=load_catalog()["ai_policy"],
    )
    assert outcome["passed"] is True
    evidence = json.loads((tmp_path / "evidence.json").read_text(encoding="utf-8"))
    evidence_text = (tmp_path / "evidence.json").read_text(encoding="utf-8")
    assert "private" not in evidence_text
    assert {record["kind"] for record in evidence["records"]} >= {"request", "response", "events", "db_snapshot", "state"}


def test_ship02_runner_rejects_privileged_fixture_command(tmp_path):
    with pytest.raises(ValueError, match="ordinary-player boundary"):
        run_ordinary_player_scenario(
            OwnershipAdapter(),
            tmp_path,
            scenario_id="V3-SHIP-02",
            case_id="unsafe-fixture",
            commands=[{"command": "sys.raw_sql_exec", "data": {"sql": "SELECT 1"}}],
            expected="refusal",
            canonical_requirements=load_catalog()["canonical_requirements"],
            ai_policy=load_catalog()["ai_policy"],
        )
