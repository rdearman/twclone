import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from qa_harness import run_ordinary_player_scenario


def test_codex4_catalog_covers_requested_v3_journeys_without_merging_policy():
    path = Path(__file__).parents[1] / "scenarios" / "v3_codex4_acceptance_catalog.json"
    catalog = json.loads(path.read_text())
    assert catalog["simulation_fixture"] is False
    ids = {scenario["id"] for scenario in catalog["scenarios"]}
    assert ids == {
        "V3-PLAN-01", "V3-SHIP-01", "V3-ECO-01", "V3-ECO-02",
        "V3-VIS-01", "V3-HAZ-01", "V3-PLAN-02", "V3-INS-01",
    }
    assert catalog["ai_policy"]["ordinary_player"] is True
    assert catalog["ai_policy"]["strategy_mode"] == "deterministic"
    assert "canonical_requirements" not in catalog["ai_policy"]
    assert "evidence_contract" in catalog


def test_catalog_keeps_insurance_and_planet_production_decision_gated():
    path = Path(__file__).parents[1] / "scenarios" / "v3_codex4_acceptance_catalog.json"
    scenarios = {item["id"]: item for item in json.loads(path.read_text())["scenarios"]}
    assert scenarios["V3-INS-01"]["status"] == "decision-gated"
    assert scenarios["V3-PLAN-02"]["status"] == "decision-gated"


class ReadOnlyFakeAdapter:
    def __init__(self):
        self.commands = []

    def read_state(self, label):
        return {"label": label, "sector_id": 20, "turns_remaining": 10}

    def send(self, command):
        self.commands.append(command)
        return {"status": "ok", "type": "trade.buy_receipt_v1", "session_token": "private"}

    def read_events(self):
        return [{"type": "trade.settled", "session_id": "private"}]

    def read_db(self, label):
        return {"label": label, "player": {"credits": 100}, "session_token": "private"}


def test_generic_runner_captures_sanitized_command_evidence(tmp_path):
    adapter = ReadOnlyFakeAdapter()
    outcome = run_ordinary_player_scenario(
        adapter,
        tmp_path,
        scenario_id="V3-ECO-01",
        case_id="buy-one",
        commands=[{"id": "buy-1", "command": "trade.buy", "data": {"quantity": 1}}],
        expected="success",
        canonical_requirements={"source": "section 19", "receipt_required": True},
        ai_policy={"strategy_mode": "deterministic", "retries": "never"},
    )

    assert outcome["passed"] is True
    evidence = json.loads((tmp_path / "evidence.json").read_text())
    assert {record["kind"] for record in evidence["records"]} == {
        "state", "db_snapshot", "request", "response", "events"
    }
    evidence_text = (tmp_path / "evidence.json").read_text()
    assert "private" not in evidence_text


def test_generic_runner_captures_read_only_boundary_for_each_command(tmp_path):
    class StableReadOnlyFakeAdapter(ReadOnlyFakeAdapter):
        def read_state(self, label):
            return {"sector_id": 20, "turns_remaining": 10}

        def read_db(self, label):
            return {"player": {"credits": 100}}

    adapter = StableReadOnlyFakeAdapter()
    outcome = run_ordinary_player_scenario(
        adapter,
        tmp_path,
        scenario_id="V3-PLAN-01",
        case_id="landing-then-readback",
        commands=[
            {"id": "land-1", "command": "planet.land", "data": {"planet_id": 42}},
            {"id": "state-1", "command": "player.my_info", "data": {}},
        ],
        expected="mixed",
        canonical_requirements={"source": "section 19", "read_only_boundaries": True},
        ai_policy={"strategy_mode": "deterministic", "retries": "never"},
    )

    assert outcome["passed"] is True
    assert outcome["command_boundaries"] == [
        {"command": 1, "state_changed": False, "database_changed": False},
        {"command": 2, "state_changed": False, "database_changed": False},
    ]
    evidence = json.loads((tmp_path / "evidence.json").read_text())
    labels = {
        record["label"] for record in evidence["records"]
        if record["kind"] in {"state", "db_snapshot"}
    }
    assert {
        "command-1-before", "command-1-after",
        "command-2-before", "command-2-after",
    } <= labels


def test_generic_runner_rejects_refusal_with_authoritative_mutation(tmp_path):
    class MutatingRefusalAdapter(ReadOnlyFakeAdapter):
        def read_state(self, label):
            return {"label": label, "sector_id": 20, "turns_remaining": 9 if label == "after" else 10}

        def read_db(self, label):
            return {"label": label, "player": {"credits": 99 if label == "after" else 100}}

        def send(self, command):
            self.commands.append(command)
            return {"status": "refused", "error": {"code": 1403}}

    outcome = run_ordinary_player_scenario(
        MutatingRefusalAdapter(),
        tmp_path,
        scenario_id="V3-PLAN-01",
        case_id="remote-refusal",
        commands=[{"id": "land-1", "command": "planet.land", "data": {"planet_id": 99}}],
        expected="refusal",
        canonical_requirements={"refusal_preserves_turn": True},
        ai_policy={"strategy_mode": "deterministic", "retries": "never"},
    )

    assert outcome["passed"] is False
    assert any("authoritative database" in error for error in outcome["errors"])


def test_generic_runner_rejects_non_success_response_in_success_journey(tmp_path):
    class RefusedResponseAdapter(ReadOnlyFakeAdapter):
        def send(self, command):
            self.commands.append(command)
            return {"status": "refused", "error": {"code": 1403}}

    outcome = run_ordinary_player_scenario(
        RefusedResponseAdapter(),
        tmp_path,
        scenario_id="V3-PLAN-01",
        case_id="landing-success-response-check",
        commands=[{"id": "land-1", "command": "planet.land", "data": {"planet_id": 42}}],
        expected="success",
        canonical_requirements={"source": "section 19"},
        ai_policy={"strategy_mode": "deterministic", "retries": "never"},
    )

    assert outcome["passed"] is False
    assert "successful scenario contained a non-success response" in outcome["errors"]


def test_generic_runner_checks_each_declared_command_boundary(tmp_path):
    class MixedRetryAdapter(ReadOnlyFakeAdapter):
        def __init__(self):
            super().__init__()
            self.index = 0

        def send(self, command):
            self.commands.append(command)
            self.index += 1
            if self.index == 1:
                return {"status": "ok", "type": "trade.buy_receipt_v1"}
            return {"status": "refused", "type": "trade.duplicate_refusal"}

        def read_state(self, label):
            # The second command is deliberately allowed to expose a bad
            # mutation; the per-command contract must catch it even though the
            # aggregate journey is classified as mixed.
            return {"sector_id": 20, "turns_remaining": 9 if "2-after" in label else 10}

        def read_db(self, label):
            return {"player": {"credits": 99 if "2-after" in label else 100}}

    outcome = run_ordinary_player_scenario(
        MixedRetryAdapter(),
        tmp_path,
        scenario_id="V3-ECO-01",
        case_id="duplicate-retry-boundaries",
        commands=[
            {"command": "trade.buy", "data": {"quantity": 1}},
            {"command": "trade.buy", "data": {"quantity": 1}},
        ],
        expected="mixed",
        canonical_requirements={
            "command_expectations": [
                {"statuses": ["ok"]},
                {
                    "statuses": ["refused", "error"],
                    "state_unchanged": True,
                    "database_unchanged": True,
                },
            ]
        },
        ai_policy={"strategy_mode": "deterministic", "retries": "never"},
    )

    assert outcome["passed"] is False
    assert "command-2 changed authoritative gameplay state" in outcome["errors"]
    assert "command-2 changed authoritative database state" in outcome["errors"]
