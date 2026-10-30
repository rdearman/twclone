import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from offline_acceptance import run_offline_acceptance


def test_five_player_acceptance_isolated_and_deterministic(tmp_path, monkeypatch):
    results = run_offline_acceptance(tmp_path / "cohort")

    assert len(results) == 5
    assert [result.identity for result in results] == [f"offline_ai_{n:02d}" for n in range(1, 6)]
    assert [result.completed for result in results] == [True, True, True, False, True]
    for result in results[:3] + results[4:]:
        assert result.credits == "1013.00"
        assert result.cargo == []
        assert result.sector == 2
        assert result.receipts == ["trade.buy_receipt_v1", "trade.sell_receipt_v1"]
        assert result.sent_commands.count("trade.buy") == 1
        assert result.sent_commands.count("trade.sell") == 1
    assert results[3].failure == "simulated warp refusal"
    assert results[3].credits == "900.00"
    assert results[3].cargo[0]["commodity"] == "ORE"
    assert results[3].sector == 1

    state_files = sorted((tmp_path / "cohort").glob("*/state.json"))
    assert len(state_files) == 5
    assert len({path.read_text() for path in state_files}) == 5


def test_offline_acceptance_has_no_network_or_ollama_dependency(tmp_path, monkeypatch):
    def forbidden(*_args, **_kwargs):
        raise AssertionError("Ollama must not be used by the offline fixture")

    monkeypatch.setattr("main.get_ollama_response", forbidden)
    results = run_offline_acceptance(tmp_path / "cohort")

    assert all("trade.buy" in result.sent_commands for result in results)
    assert all("trade.sell" in result.sent_commands for result in results if result.failure is None)
    assert "192.168." not in json.dumps(results, default=lambda item: item.__dict__)


def test_offline_acceptance_rejects_non_five_player_runs(tmp_path):
    with pytest.raises(ValueError, match="exactly five"):
        run_offline_acceptance(tmp_path / "cohort", player_count=4)
