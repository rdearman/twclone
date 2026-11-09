import json
from pathlib import Path
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from qa_harness import run_ordinary_player_scenario


SCENARIO_PATH = Path(__file__).parents[1] / "scenarios" / "v3_codex4_ordinary_player_scenarios.json"
EXPECTED_REQUIREMENTS = {
    "V3-PLAN-01", "V3-SHIP-01", "V3-ECO-01", "V3-ECO-02",
    "V3-VIS-01", "V3-HAZ-01", "V3-PLAN-02", "V3-INS-01",
}


def load_catalog():
    return json.loads(SCENARIO_PATH.read_text(encoding="utf-8"))


def test_scenario_matrix_covers_requested_requirements_and_safe_evidence_contract():
    catalog = load_catalog()
    assert catalog["simulation_fixture"] is False
    assert catalog["execution_mode"] == "disposable-live-adapter-only"
    assert {item["requirement_id"] for item in catalog["scenarios"]} == EXPECTED_REQUIREMENTS
    assert "database snapshot before/after" in catalog["evidence_contract"]["each_command"]
    assert "no provisioning or direct row writes" in catalog["evidence_contract"]["safety"]
    assert "canonical_requirements" not in catalog["ai_policy"]


def test_each_case_is_replayable_and_has_dependency_note():
    catalog = load_catalog()
    for scenario in catalog["scenarios"]:
        assert scenario["dependency_note"]
        assert scenario["restart_required"] is True
        assert scenario["cases"]
        for case in scenario["cases"]:
            assert case["id"]
            assert case["commands"]
            assert case["expected"] in {"success", "refusal", "mixed", "deferred"}
            assert case["assertions"]
            for command in case["commands"]:
                assert command["command"]
                assert isinstance(command["data"], dict)


def test_runner_derives_stable_replay_ids_without_mutating_catalog(tmp_path):
    adapter = LandingReadbackAdapter(restart=False)
    adapter.read_restart_state = None
    adapter.read_restart_db = None
    original = {"command": "player.my_info", "data": {}}
    outcome = run_ordinary_player_scenario(
        adapter, tmp_path,
        scenario_id="V3-VIS-01",
        case_id="remote-refusal",
        commands=[original],
        expected="success",
        canonical_requirements={"restart_required": False},
        ai_policy={"strategy_mode": "deterministic", "retries": "never"},
    )

    assert outcome["passed"] is True
    assert original == {"command": "player.my_info", "data": {}}
    evidence = json.loads((tmp_path / "evidence.json").read_text())
    request = next(record for record in evidence["records"] if record["kind"] == "request")
    assert request["payload"]["id"] == "V3-VIS-01:remote-refusal:command-1"


def test_live_execution_is_only_unblocked_for_selected_landing_requirement():
    catalog = load_catalog()
    status = {item["requirement_id"]: item["execution_status"] for item in catalog["scenarios"]}
    assert status["V3-PLAN-01"] == "next-unblocked"
    assert all(status[item] != "next-unblocked" for item in EXPECTED_REQUIREMENTS - {"V3-PLAN-01"})


def test_selected_landing_journey_includes_authoritative_readback():
    catalog = load_catalog()
    landing = next(item for item in catalog["scenarios"] if item["requirement_id"] == "V3-PLAN-01")
    case = next(item for item in landing["cases"] if item["id"] == "valid-owned-readback")
    assert [command["command"] for command in case["commands"]] == [
        "planet.land", "player.my_info"
    ]
    assert "database boundaries" in " ".join(case["assertions"])


def test_selected_landing_covers_every_section_19_negative_case():
    catalog = load_catalog()
    landing = next(item for item in catalog["scenarios"]
                   if item["requirement_id"] == "V3-PLAN-01")
    case_ids = {case["id"] for case in landing["cases"]}
    assert {
        "malformed-refusal",
        "remote-refusal",
        "unauthorized-refusal",
        "zero-turn-refusal",
        "defense-refusal",
        "persistence-failure-refusal",
    } <= case_ids


def test_selected_landing_covers_owned_unowned_and_reland_positive_journeys():
    catalog = load_catalog()
    landing = next(item for item in catalog["scenarios"]
                   if item["requirement_id"] == "V3-PLAN-01")
    cases = {case["id"]: case for case in landing["cases"]}
    assert {"valid-owned", "valid-unowned", "valid-reland"} <= set(cases)
    assert cases["valid-unowned"]["commands"][0]["data"]["planet_id"] == "unowned_same_sector"
    assert len(cases["valid-reland"]["commands"]) == 2
    assert all(command["command"] == "planet.land"
               for command in cases["valid-reland"]["commands"])
    assert "two distinct landing events" in " ".join(cases["valid-reland"]["assertions"])


def test_reland_journey_records_each_command_boundary_and_restart(tmp_path):
    class RelandAdapter:
        def __init__(self):
            self.index = 0

        def read_state(self, label):
            turns = 10 if label in {"before", "command-1-before"} else 9
            if label in {"command-2-after", "after", "after_restart"}:
                turns = 8
            return {"sector_id": 20, "turns_remaining": turns,
                    "landed_planet": 42, "onplanet": turns < 10}

        def read_db(self, label):
            turns = 10 if label in {"before", "command-1-before"} else 9
            if label in {"command-2-after", "after", "after_restart"}:
                turns = 8
            return {"player": {"turns": turns},
                    "ship": {"onplanet": turns < 10, "planet_id": 42}}

        def send(self, command):
            self.index += 1
            return {"status": "ok", "type": "planet.land.success",
                    "data": {"planet_id": 42}}

        def read_events(self):
            return [{"type": "planet.land", "planet_id": 42,
                     "sequence": self.index}]

        def read_restart_state(self):
            return self.read_state("after_restart")

        def read_restart_db(self):
            return self.read_db("after_restart")

    outcome = run_ordinary_player_scenario(
        RelandAdapter(), tmp_path,
        scenario_id="V3-PLAN-01-ordinary-landing-atomicity",
        case_id="valid-reland",
        commands=[
            {"command": "planet.land", "data": {"planet_id": "owned_same_sector"}},
            {"command": "planet.land", "data": {"planet_id": "owned_same_sector"}},
        ],
        expected="success",
        canonical_requirements={
            "restart_required": True,
            "response_types": ["planet.land.success", "planet.land.success"],
            "final_state": {"turns_remaining": 8, "landed_planet": 42},
            "database_must_change": True,
            "event_required": True,
        },
        ai_policy={"strategy_mode": "deterministic", "retries": "never"},
    )

    assert outcome["passed"] is True
    assert outcome["response_count"] == 2
    assert outcome["event_count"] == 2
    assert outcome["restart_checked"] is True
    assert [boundary["state_changed"] for boundary in outcome["command_boundaries"]] == [True, True]


def test_catalog_journeys_use_only_ordinary_player_commands():
    catalog = load_catalog()
    forbidden = ("admin.", "sys.", "migrate", "database.reset", "fixture.seed", "sql.exec")
    for scenario in catalog["scenarios"]:
        for case in scenario["cases"]:
            for command in case["commands"]:
                name = command["command"]
                assert not name.startswith(("admin.", "sys."))
                assert name not in forbidden


def test_harness_rejects_privileged_command_before_adapter_reads(tmp_path):
    class ReadGuardAdapter(LandingReadbackAdapter):
        def read_state(self, label):
            raise AssertionError("adapter must not be read for a forbidden journey")

    with pytest.raises(ValueError, match="ordinary-player boundary"):
        run_ordinary_player_scenario(
            ReadGuardAdapter(restart=False),
            tmp_path,
            scenario_id="V3-PLAN-01",
            case_id="unsafe-fixture-command",
            commands=[{"command": "sys.raw_sql_exec", "data": {}}],
            expected="refusal",
            canonical_requirements={},
            ai_policy={"strategy_mode": "deterministic"},
        )


class LandingReadbackAdapter:
    def __init__(self, *, restart=True, restart_state=None):
        self.initial = {"sector_id": 20, "turns_remaining": 10, "landed_planet": None, "onplanet": False}
        self.landed = {"sector_id": 20, "turns_remaining": 9, "landed_planet": 42, "onplanet": True}
        self.initial_db = {"player": {"turns": 10}, "ship": {"onplanet": False}}
        self.landed_db = {"player": {"turns": 9}, "ship": {"onplanet": True, "planet_id": 42}}
        self.restart = restart
        self.restart_state = restart_state if restart_state is not None else self.landed
        self.responses = [
            {"status": "ok", "type": "planet.land.success", "data": {"planet_id": 42}},
            {"status": "ok", "type": "player.my_info", "data": {"landed_planet": 42}},
        ]
        self.index = 0

    def read_state(self, label):
        return dict(self.initial if label in {"before", "command-1-before"} else self.landed)

    def read_db(self, label):
        return dict(self.initial_db if label in {"before", "command-1-before"} else self.landed_db)

    def send(self, command):
        response = self.responses[self.index]
        self.index += 1
        return response

    def read_events(self):
        return [{"type": "planet.land"}] if self.index == 1 else []

    def read_restart_state(self):
        if not self.restart:
            raise AssertionError("restart reader should not be called")
        return dict(self.restart_state)

    def read_restart_db(self):
        if not self.restart:
            raise AssertionError("restart reader should not be called")
        return dict(self.landed_db)


def test_selected_landing_readback_enforces_response_state_and_restart_contract(tmp_path):
    outcome = run_ordinary_player_scenario(
        LandingReadbackAdapter(), tmp_path,
        scenario_id="V3-PLAN-01-ordinary-landing-atomicity",
        case_id="valid-owned-readback",
        commands=[
            {"id": "land-1", "command": "planet.land", "data": {"planet_id": "owned_same_sector"}},
            {"id": "info-1", "command": "player.my_info", "data": {}},
        ],
        expected="success",
        canonical_requirements={
            "restart_required": True,
            "response_types": ["planet.land.success", "player.my_info"],
            "final_state": {"landed_planet": 42, "onplanet": True},
            "database_must_change": True,
            "event_required": True,
        },
        ai_policy={"strategy_mode": "deterministic", "retries": "never"},
    )

    assert outcome["passed"] is True
    assert outcome["restart_checked"] is True
    assert outcome["response_types"] == ["planet.land.success", "player.my_info"]


def test_selected_landing_readback_fails_without_post_restart_evidence(tmp_path):
    adapter = LandingReadbackAdapter()
    adapter.read_restart_state = None
    adapter.read_restart_db = None

    outcome = run_ordinary_player_scenario(
        adapter, tmp_path,
        scenario_id="V3-PLAN-01-ordinary-landing-atomicity",
        case_id="restart-missing",
        commands=[{"id": "land-1", "command": "planet.land", "data": {"planet_id": 42}}],
        expected="success",
        canonical_requirements={"restart_required": True},
        ai_policy={"strategy_mode": "deterministic"},
    )

    assert outcome["passed"] is False
    assert outcome["restart_checked"] is False
    assert any("restart-required" in error for error in outcome["errors"])


def test_selected_landing_readback_fails_when_restart_state_disagrees(tmp_path):
    outcome = run_ordinary_player_scenario(
        LandingReadbackAdapter(restart_state={"sector_id": 20, "turns_remaining": 8}), tmp_path,
        scenario_id="V3-PLAN-01-ordinary-landing-atomicity",
        case_id="restart-mismatch",
        commands=[{"id": "land-1", "command": "planet.land", "data": {"planet_id": 42}}],
        expected="success",
        canonical_requirements={"restart_required": True},
        ai_policy={"strategy_mode": "deterministic"},
    )

    assert outcome["passed"] is False
    assert any("post-restart gameplay state" in error for error in outcome["errors"])
