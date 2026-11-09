import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from qa_harness import run_planet_landing


class FakeLandingAdapter:
    def __init__(self, response, before, after, events, db_before, db_after):
        self.response = response
        self.before = before
        self.after = after
        self.events = events
        self.db_before = db_before
        self.db_after = db_after
        self.commands = []

    def read_state(self, label):
        return dict(self.before if label == "before" else self.after)

    def send(self, command):
        self.commands.append(command)
        return self.response

    def read_events(self):
        return list(self.events)

    def read_db(self, label):
        return dict(self.db_before if label == "before" else self.db_after)


def test_landing_harness_captures_success_and_separates_policy(tmp_path):
    adapter = FakeLandingAdapter(
        {"status": "ok", "type": "planet.land.success", "data": {"planet_id": 42}},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None, "onplanet": False, "ship_id": 7},
        {"sector_id": 20, "turns_remaining": 9, "landed_planet": 42, "onplanet": True, "ship_id": 7},
        [{"type": "planet.land", "planet_id": 42}],
        {"player": {"turns": 10}, "ship": {"onplanet": False}},
        {"player": {"turns": 9}, "ship": {"onplanet": True, "planet_id": 42}},
    )

    outcome = run_planet_landing(
        adapter, tmp_path, planet_id=42, case_id="valid-owned", expected="success",
        canonical_requirements={"turn_cost": "V3-CAN-00", "event_required": True},
        ai_policy={"strategy_mode": "deterministic", "retries": "never"},
    )

    assert outcome["passed"] is True
    assert adapter.commands[0]["command"] == "planet.land"
    evidence = json.loads((tmp_path / "evidence.json").read_text())
    assert evidence["canonical_requirements"]["turn_cost"] == "V3-CAN-00"
    assert evidence["ai_policy"]["strategy_mode"] == "deterministic"
    assert {record["kind"] for record in evidence["records"]} == {
        "state", "db_snapshot", "request", "response", "events"
    }
    assert evidence["outcome"]["database_changed"] is True
    assert evidence["outcome"]["event_types"] == ["planet.land"]


def test_landing_harness_checks_explicit_numeric_turn_cost(tmp_path):
    adapter = FakeLandingAdapter(
        {"status": "ok", "type": "planet.land.success"},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None, "onplanet": False},
        {"sector_id": 20, "turns_remaining": 8, "landed_planet": 42, "onplanet": True},
        [{"type": "planet.land", "planet_id": 42}],
        {"player": {"turns": 10}}, {"player": {"turns": 8}},
    )

    outcome = run_planet_landing(
        adapter, tmp_path, planet_id=42, case_id="valid-two-turn-cost", expected="success",
        canonical_requirements={"turn_cost": 2},
        ai_policy={"strategy_mode": "deterministic"},
    )

    assert outcome["passed"] is True


def test_success_requires_declared_event_and_database_persistence(tmp_path):
    adapter = FakeLandingAdapter(
        {"status": "ok", "type": "planet.land.success", "data": {"planet_id": 42}},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None, "onplanet": False},
        {"sector_id": 20, "turns_remaining": 9, "landed_planet": 42, "onplanet": True},
        [],
        {"player": {"turns": 10}},
        {"player": {"turns": 10}},
    )

    outcome = run_planet_landing(
        adapter, tmp_path, planet_id=42, case_id="success-without-persistence", expected="success",
        canonical_requirements={
            "turn_cost": 1,
            "event_required": True,
            "database_must_change": True,
        },
        ai_policy={"strategy_mode": "deterministic"},
    )

    assert outcome["passed"] is False
    assert any("landing event" in error for error in outcome["errors"])
    assert any("database snapshot" in error for error in outcome["errors"])


def test_success_rejects_response_for_a_different_planet(tmp_path):
    adapter = FakeLandingAdapter(
        {"status": "ok", "type": "planet.land.success", "data": {"planet_id": 7}},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None, "onplanet": False},
        {"sector_id": 20, "turns_remaining": 9, "landed_planet": 42, "onplanet": True},
        [{"type": "planet.land", "planet_id": 42}],
        {"player": {"turns": 10}},
        {"player": {"turns": 9, "planet_id": 42}},
    )

    outcome = run_planet_landing(
        adapter, tmp_path, planet_id=42, case_id="response-identity-mismatch", expected="success",
        canonical_requirements={"turn_cost": 1},
        ai_policy={"strategy_mode": "deterministic"},
    )

    assert outcome["passed"] is False
    assert any("different planet" in error for error in outcome["errors"])


def test_landing_refusal_rejects_database_mutation(tmp_path):
    adapter = FakeLandingAdapter(
        {"status": "refused", "error": {"code": 1403}},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None, "onplanet": False},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None, "onplanet": False},
        [], {"player": {"turns": 10}}, {"player": {"turns": 9}},
    )

    outcome = run_planet_landing(
        adapter, tmp_path, planet_id=99, case_id="remote-db-mutation", expected="refusal",
        canonical_requirements={"refusal_preserves_turn": True},
        ai_policy={"strategy_mode": "deterministic"},
    )

    assert outcome["passed"] is False
    assert "authoritative database snapshot" in outcome["errors"][0]


def test_landing_refusal_is_state_atomic_and_redacts_session(tmp_path):
    adapter = FakeLandingAdapter(
        {"status": "refused", "error": {"code": 1403, "session_token": "secret"}},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None, "onplanet": False, "ship_id": 7},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None, "onplanet": False, "ship_id": 7},
        [], {"turns": 10}, {"turns": 10},
    )

    outcome = run_planet_landing(
        adapter, tmp_path, planet_id=99, case_id="remote-refusal", expected="refusal",
        canonical_requirements={"refusal_preserves_turn": True},
        ai_policy={"session_token": "must-not-leak"},
    )

    assert outcome["passed"] is True
    evidence_text = (tmp_path / "evidence.json").read_text()
    assert "secret" not in evidence_text
    assert "must-not-leak" not in evidence_text
    evidence = json.loads(evidence_text)
    assert evidence["outcome"]["database_changed"] is False
    assert evidence["outcome"]["event_count"] == 0


def test_malformed_landing_refusal_preserves_turn_and_landing_state(tmp_path):
    """The malformed ordinary-player request is captured as a no-op refusal."""
    adapter = FakeLandingAdapter(
        {"status": "error", "type": "planet.land.failure", "error": {"code": "INVALID_ARG"}},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None,
         "onplanet": False, "ship_id": 7},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None,
         "onplanet": False, "ship_id": 7},
        [],
        {"player": {"turns": 10}, "ship": {"onplanet": False}},
        {"player": {"turns": 10}, "ship": {"onplanet": False}},
    )

    outcome = run_planet_landing(
        adapter, tmp_path, planet_id=0, case_id="malformed-refusal", expected="refusal",
        canonical_requirements={
            "refusal_preserves_turn": True,
            "database_must_be_unchanged": True,
        },
        ai_policy={"strategy_mode": "deterministic", "retries": "never"},
    )

    assert outcome["passed"] is True
    assert adapter.commands == [{
        "id": "planet-land-1",
        "command": "planet.land",
        "data": {"planet_id": 0},
    }]
    assert outcome["state_delta"] == {}
    assert outcome["database_changed"] is False


def test_landing_fails_closed_when_adapter_omits_evidence_boundaries(tmp_path):
    class IncompleteEvidenceAdapter(FakeLandingAdapter):
        def read_events(self):
            return None

        def read_db(self, label):
            return None if label == "after" else super().read_db(label)

    adapter = IncompleteEvidenceAdapter(
        {"status": "refused", "error": {"code": 1403}},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None, "onplanet": False},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None, "onplanet": False},
        [], {"player": {"turns": 10}}, {"player": {"turns": 10}},
    )

    outcome = run_planet_landing(
        adapter, tmp_path, planet_id=99, case_id="missing-evidence", expected="refusal",
        canonical_requirements={"database_must_be_unchanged": True},
        ai_policy={"strategy_mode": "deterministic"},
    )

    assert outcome["passed"] is False
    assert any("database snapshot after evidence" in error for error in outcome["errors"])
    assert any("event batch evidence" in error for error in outcome["errors"])
    evidence = json.loads((tmp_path / "evidence.json").read_text())
    assert any(record["kind"] == "db_snapshot" and record["payload"] is None
               for record in evidence["records"])


def test_persistence_failure_refusal_captures_rollback_boundaries(tmp_path):
    """A failed landing commit must leave turn and landing state unchanged."""
    adapter = FakeLandingAdapter(
        {
            "status": "error",
            "type": "planet.land.failure",
            "error": {"code": "DB_FAILURE", "message": "transaction rolled back"},
        },
        {
            "sector_id": 20,
            "turns_remaining": 10,
            "landed_planet": None,
            "onplanet": False,
            "ship_id": 7,
        },
        {
            "sector_id": 20,
            "turns_remaining": 10,
            "landed_planet": None,
            "onplanet": False,
            "ship_id": 7,
        },
        [],
        {"player": {"turns": 10}, "ship": {"onplanet": False}},
        {"player": {"turns": 10}, "ship": {"onplanet": False}},
    )

    outcome = run_planet_landing(
        adapter,
        tmp_path,
        planet_id=42,
        case_id="persistence-failure-refusal",
        expected="refusal",
        canonical_requirements={
            "refusal_preserves_turn": True,
            "database_must_be_unchanged": True,
        },
        ai_policy={"strategy_mode": "deterministic", "retries": "never"},
    )

    assert outcome["passed"] is True
    assert outcome["response_status"] == "error"
    assert outcome["response_type"] == "planet.land.failure"
    assert outcome["state_delta"] == {}
    assert outcome["database_changed"] is False
    evidence = json.loads((tmp_path / "evidence.json").read_text())
    assert {record["kind"] for record in evidence["records"]} == {
        "state", "db_snapshot", "request", "response", "events"
    }


def test_defense_refusal_allows_recorded_damage_but_preserves_turn_and_landing(tmp_path):
    adapter = FakeLandingAdapter(
        {"status": "refused", "type": "planet.land.refused", "error": {"code": 1403}},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None, "onplanet": False,
         "ship_id": 7, "hull": 200},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None, "onplanet": False,
         "ship_id": 7, "hull": 100},
        [{"type": "combat.hit.quasar", "damage": 100}],
        {"player": {"turns": 10}, "ship": {"onplanet": False, "hull": 200}},
        {"player": {"turns": 10}, "ship": {"onplanet": False, "hull": 100}},
    )

    outcome = run_planet_landing(
        adapter, tmp_path, planet_id=42, case_id="defense-refusal", expected="refusal",
        canonical_requirements={
            "refusal_preserves_turn": True,
            "database_must_be_unchanged": False,
        },
        ai_policy={"strategy_mode": "deterministic", "retries": "never"},
    )

    assert outcome["passed"] is True
    assert outcome["database_changed"] is True
    assert outcome["event_types"] == ["combat.hit.quasar"]


def test_restart_required_case_rejects_adapter_without_restart_readback(tmp_path):
    adapter = FakeLandingAdapter(
        {"status": "ok", "type": "planet.land.success", "data": {"planet_id": 42}},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None,
         "onplanet": False},
        {"sector_id": 20, "turns_remaining": 9, "landed_planet": 42,
         "onplanet": True},
        [{"type": "planet.land", "planet_id": 42}],
        {"player": {"turns": 10}}, {"player": {"turns": 9, "planet_id": 42}},
    )

    outcome = run_planet_landing(
        adapter, tmp_path, planet_id=42, case_id="restart-missing", expected="success",
        canonical_requirements={"turn_cost": 1, "restart_required": True},
        ai_policy={"strategy_mode": "deterministic", "retries": "never"},
    )

    assert outcome["passed"] is False
    assert outcome["restart_checked"] is False
    assert any("post-restart" in error for error in outcome["errors"])


def test_restart_readback_is_recorded_and_must_match_immediate_state(tmp_path):
    class RestartableFakeAdapter(FakeLandingAdapter):
        def read_restart_state(self):
            return dict(self.after)

        def read_restart_db(self):
            return dict(self.db_after)

    adapter = RestartableFakeAdapter(
        {"status": "ok", "type": "planet.land.success", "data": {"planet_id": 42}},
        {"sector_id": 20, "turns_remaining": 10, "landed_planet": None,
         "onplanet": False},
        {"sector_id": 20, "turns_remaining": 9, "landed_planet": 42,
         "onplanet": True},
        [{"type": "planet.land", "planet_id": 42}],
        {"player": {"turns": 10}}, {"player": {"turns": 9, "planet_id": 42}},
    )

    outcome = run_planet_landing(
        adapter, tmp_path, planet_id=42, case_id="restart-match", expected="success",
        canonical_requirements={"turn_cost": 1, "restart_required": True},
        ai_policy={"strategy_mode": "deterministic", "retries": "never"},
    )

    assert outcome["passed"] is True
    assert outcome["restart_checked"] is True
    evidence = json.loads((tmp_path / "evidence.json").read_text())
    assert {record.get("label") for record in evidence["records"] if record["kind"] == "state"} >= {
        "before", "after", "after_restart"
    }


def test_scenario_spec_keeps_deferred_requirements_visible():
    path = Path(__file__).parents[1] / "scenarios" / "v3_planet_landing_atomicity.json"
    spec = json.loads(path.read_text())
    assert spec["requirement"] == "V3-PLAN-01"
    assert spec["ai_policy"]["strategy_mode"] == "deterministic"
    assert spec["canonical_requirements"]["restart_required"] is True
    assert "V3-ECO-02 bank interest" in spec["deferred_catalog"]
    assert any("turn" in item.lower() for item in spec["canonical_requirements"]["negative"])
    assert any("snapshot" in item.lower() for item in spec["canonical_requirements"]["negative"])
    assert {case["id"] for case in spec["cases"]} >= {
        "malformed-refusal", "valid-owned", "remote-refusal",
        "unauthorized-refusal", "zero-turn-refusal", "defense-refusal",
        "persistence-failure-refusal",
    }
