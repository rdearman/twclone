"""Contract checks for the issue #490 ordinary-player scenario bundle.

These checks validate the QA boundary only.  They do not stand in for a live
server run and deliberately do not provision, migrate, or write database rows.
"""

import json
from pathlib import Path


ROOT = Path(__file__).parents[1]
CATALOG = ROOT / "scenarios" / "v3_codex4_ordinary_player_scenarios.json"

EXPECTED = {
    "V3-PLAN-01",
    "V3-SHIP-01",
    "V3-ECO-01",
    "V3-ECO-02",
    "V3-VIS-01",
    "V3-HAZ-01",
    "V3-PLAN-02",
    "V3-INS-01",
}


def load_catalog():
    return json.loads(CATALOG.read_text(encoding="utf-8"))


def test_requested_scenarios_have_complete_read_only_evidence_contract():
    catalog = load_catalog()
    assert {item["requirement_id"] for item in catalog["scenarios"]} == EXPECTED
    assert catalog["execution_mode"] == "disposable-live-adapter-only"
    assert "response" in catalog["evidence_contract"]["each_command"]
    assert "event batch" in catalog["evidence_contract"]["each_command"]
    assert "state before/after" in catalog["evidence_contract"]["each_command"]
    assert "database snapshot before/after" in catalog["evidence_contract"]["each_command"]
    assert "no provisioning or direct row writes" in catalog["evidence_contract"]["safety"]
    assert "never replay an ambiguous mutation" in catalog["evidence_contract"]["safety"]


def test_only_selected_planet_landing_slice_is_live_eligible():
    statuses = {
        item["requirement_id"]: item["execution_status"]
        for item in load_catalog()["scenarios"]
    }
    assert statuses["V3-PLAN-01"] == "next-unblocked"
    assert all(
        status != "next-unblocked"
        for requirement_id, status in statuses.items()
        if requirement_id != "V3-PLAN-01"
    )


def test_every_case_is_replayable_and_ordinary_player_only():
    forbidden_prefixes = ("admin.", "sys.")
    forbidden_commands = {"migrate", "database.reset", "fixture.seed", "sql.exec"}

    for scenario in load_catalog()["scenarios"]:
        assert scenario["dependency_note"]
        assert scenario["restart_required"] is True
        for case in scenario["cases"]:
            assert case["id"]
            assert case["commands"]
            assert case["assertions"]
            for command in case["commands"]:
                name = command["command"]
                assert not name.startswith(forbidden_prefixes)
                assert name not in forbidden_commands
                assert isinstance(command["data"], dict)


def test_every_requested_journey_declares_enforceable_canonical_boundaries():
    """Scenario prose cannot substitute for the evidence contract.

    These are deliberately invariant/observation requirements.  They do not
    choose edition-sensitive rates, formulas, or insurance applicability.
    """
    for scenario in load_catalog()["scenarios"]:
        requirements = scenario.get("canonical_requirements")
        assert isinstance(requirements, dict)
        assert requirements["evidence"] == [
            "response", "events", "state_before_after", "database_before_after"
        ]
        assert requirements["restart_readback"] is True
        if scenario["requirement_id"] == "V3-ECO-02":
            assert requirements["one_daily_application"] is True
            assert requirements["same_day_idempotent"] is True
            assert requirements["ledger_reconciles"] is True
        if scenario["requirement_id"] == "V3-INS-01":
            assert requirements["disposition_required"] is True
            assert requirements["no_fabricated_success"] is True


def test_planet_landing_slice_contains_refusal_success_and_readback_boundaries():
    landing = next(
        item for item in load_catalog()["scenarios"]
        if item["requirement_id"] == "V3-PLAN-01"
    )
    cases = {case["id"]: case for case in landing["cases"]}
    assert {
        "malformed-refusal",
        "remote-refusal",
        "unauthorized-refusal",
        "zero-turn-refusal",
        "defense-refusal",
        "persistence-failure-refusal",
        "valid-owned",
        "valid-unowned",
        "valid-reland",
        "valid-owned-readback",
    } <= set(cases)
    assert [command["command"] for command in cases["valid-owned-readback"]["commands"]] == [
        "planet.land",
        "player.my_info",
    ]
    assert "database boundaries" in " ".join(cases["valid-owned-readback"]["assertions"])
