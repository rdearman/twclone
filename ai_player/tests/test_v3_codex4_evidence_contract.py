"""Fail-closed contract checks for the issue #490 scenario catalog.

These checks validate QA evidence shape only.  They must not turn a fixture or
mocked adapter result into gameplay acceptance.
"""

import json
from pathlib import Path


CATALOG = Path(__file__).parents[1] / "scenarios" / "v3_codex4_ordinary_player_scenarios.json"
REQUIRED_IDS = {
    "V3-PLAN-01",
    "V3-SHIP-01",
    "V3-ECO-01",
    "V3-ECO-02",
    "V3-VIS-01",
    "V3-HAZ-01",
    "V3-PLAN-02",
    "V3-INS-01",
}
REQUIRED_EVIDENCE = {
    "response",
    "events",
    "state_before_after",
    "database_before_after",
}


def load_catalog():
    return json.loads(CATALOG.read_text(encoding="utf-8"))


def test_every_requested_scenario_has_fail_closed_evidence_and_policy_boundaries():
    catalog = load_catalog()
    scenarios = {item["requirement_id"]: item for item in catalog["scenarios"]}

    assert set(scenarios) == REQUIRED_IDS
    assert catalog["simulation_fixture"] is False
    assert catalog["execution_mode"] == "disposable-live-adapter-only"
    assert set(catalog["evidence_contract"]["each_command"]) >= {
        "response",
        "event batch",
        "state before/after",
        "database snapshot before/after",
    }
    assert catalog["ai_policy"]["ordinary_player"] is True

    for requirement_id, scenario in scenarios.items():
        canonical = scenario["canonical_requirements"]
        assert set(canonical["evidence"]) >= REQUIRED_EVIDENCE, requirement_id
        assert scenario["restart_required"] is True, requirement_id
        assert scenario["dependency_note"], requirement_id
        assert scenario["cases"], requirement_id
        for case in scenario["cases"]:
            assert case["id"], requirement_id
            assert case["assertions"], f"{requirement_id}:{case['id']}"
            for command in case["commands"]:
                name = command["command"]
                assert not name.startswith(("admin.", "sys.")), name
                assert name not in {"migrate", "database.reset", "fixture.seed", "sql.exec"}
                assert isinstance(command["data"], dict)


def test_insurance_never_converts_unresolved_policy_into_success():
    insurance = next(
        item for item in load_catalog()["scenarios"] if item["requirement_id"] == "V3-INS-01"
    )
    assert insurance["execution_status"] == "decision-gated"
    assert insurance["canonical_requirements"]["disposition_required"] is True
    assert insurance["canonical_requirements"]["no_fabricated_success"] is True
    assert any(case["expected"] == "deferred" for case in insurance["cases"])
