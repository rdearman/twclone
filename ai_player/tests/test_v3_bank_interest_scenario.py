import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from qa_harness import run_ordinary_player_scenario


SCENARIO = Path(__file__).parents[1] / "scenarios" / "v3_bank_interest_runtime.json"


class BankAdapter:
    def __init__(self, *, eligible=True, failure=False):
        self.eligible = eligible
        self.failure = failure
        self.initial = {"bank_balance": 1000, "interest_date": "2026-10-31"}
        self.after = {
            "bank_balance": 1010 if eligible and not failure else 1000,
            "interest_date": "2026-10-31",
        }
        self.initial_db = {"bank": {"balance": 1000, "ledger_count": 4}}
        self.after_db = {
            "bank": {
                "balance": 1010 if eligible and not failure else 1000,
                "ledger_count": 5 if eligible and not failure else 4,
            }
        }

    def read_state(self, label):
        return dict(self.initial if label in {"before", "command-1-before"} else self.after)

    def read_db(self, label):
        return dict(self.initial_db if label in {"before", "command-1-before"} else self.after_db)

    def send(self, command):
        if self.failure:
            return {"status": "error", "type": "bank.interest.error", "error": {"code": "LEDGER_FAILURE"}}
        return {"status": "ok", "type": "player.my_info", "data": {"bank_balance": self.after["bank_balance"]}}

    def read_events(self):
        return [] if self.failure or not self.eligible else [{"type": "bank.interest.applied", "amount": 10}]

    def read_restart_state(self):
        return dict(self.after)

    def read_restart_db(self):
        return dict(self.after_db)


def _manifest():
    return json.loads(SCENARIO.read_text(encoding="utf-8"))


def test_bank_scenario_separates_canonical_requirements_from_ai_policy():
    manifest = _manifest()
    assert manifest["execution_status"] == "deferred-until-engine-wiring"
    assert manifest["simulation_fixture"] is False
    assert "canonical_requirements" not in manifest["ai_policy"]
    assert "no duplicate event" in manifest["cases"][-1]["assertions"]


def test_eligible_interest_captures_event_ledger_delta_and_restart(tmp_path):
    manifest = _manifest()
    outcome = run_ordinary_player_scenario(
        BankAdapter(), tmp_path,
        scenario_id=manifest["scenario_id"],
        case_id="eligible-daily-accrual",
        commands=manifest["cases"][0]["commands"],
        expected="success",
        canonical_requirements={
            "restart_required": True,
            "response_types": ["player.my_info"],
            "final_state": {"bank_balance": 1010},
            "database_must_change": True,
            "event_required": True,
        },
        ai_policy=manifest["ai_policy"],
    )
    assert outcome["passed"] is True
    assert outcome["db_changed"] is True
    assert outcome["restart_checked"] is True


def test_ledger_failure_is_captured_as_refusal_without_state_or_db_change(tmp_path):
    manifest = _manifest()
    outcome = run_ordinary_player_scenario(
        BankAdapter(failure=True), tmp_path,
        scenario_id=manifest["scenario_id"],
        case_id="ledger-failure-rollback",
        commands=manifest["cases"][-1]["commands"],
        expected="refusal",
        canonical_requirements={
            "restart_required": True,
            "database_must_be_unchanged": True,
        },
        ai_policy=manifest["ai_policy"],
    )
    assert outcome["passed"] is True
    assert outcome["db_changed"] is False
    evidence = json.loads((tmp_path / "evidence.json").read_text(encoding="utf-8"))
    assert any(record["kind"] == "response" for record in evidence["records"])
    assert any(record["kind"] == "events" for record in evidence["records"])
