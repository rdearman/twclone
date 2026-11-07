import sys
from pathlib import Path

AI_PLAYER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AI_PLAYER_DIR))

from v3_capabilities import (  # noqa: E402
    build_system_hello,
    build_player_command,
    capability_snapshot,
    is_safe_player_command,
    normalize_command_catalogue,
)


def test_catalogue_normalizes_public_names_and_rejects_non_requests():
    payload = {"commands": [
        {"cmd": "move.warp"},
        {"command": "planet.info"},
        {"name": "ship.info"},
        "sysop.shutdown",
        "engine.tick",
        "move.result",
        {"cmd": "planet.info"},
    ]}
    assert normalize_command_catalogue(payload) == ["move.warp", "planet.info", "ship.info"]


def test_handshake_is_unauthenticated_and_declares_safety_features():
    hello = build_system_hello({"client_version": "test-ai/3"})
    assert hello["command"] == "system.hello"
    assert hello["data"]["client_version"] == "test-ai/3"
    assert "idempotency" in hello["data"]["capabilities"]
    assert "session_token" not in hello["meta"]


def test_capability_snapshot_reports_schema_gap():
    snapshot = capability_snapshot(["ship.info", "move.warp"], {"ship.info": {}})
    assert snapshot["command_count"] == 2
    assert snapshot["schema_count"] == 1
    assert snapshot["missing_schemas"] == ["move.warp"]


def test_privileged_and_response_names_are_not_safe_requests():
    assert is_safe_player_command("trade.buy")
    assert not is_safe_player_command("sysop.shutdown")
    assert not is_safe_player_command("engine.tick")
    assert not is_safe_player_command("move.result")


def test_generic_executor_adds_trade_idempotency_and_enforces_inventory():
    command = build_player_command(
        "trade.buy", {"port_id": 4}, "session", "ai/3",
        allowed_commands=["trade.buy"], idempotency_key="operation-1234",
    )
    assert command["data"]["idempotency_key"] == "operation-1234"
    assert command["meta"]["session_token"] == "session"


def test_generic_executor_rejects_unadvertised_or_privileged_commands():
    try:
        build_player_command("sysop.shutdown", {}, None, "ai/3")
    except ValueError:
        pass
    else:
        raise AssertionError("privileged command was accepted")
    try:
        build_player_command("trade.sell", {}, None, "ai/3", ["trade.buy"])
    except ValueError:
        pass
    else:
        raise AssertionError("unadvertised command was accepted")


def test_exhaustive_qa_inventory_excludes_destructive_actions():
    import main

    assert "v3_inventory" in main.QA_WORKFLOWS
    inventory = set(main.QA_WORKFLOWS["v3_inventory"])
    assert "ship.self_destruct" not in inventory
    assert "corp.dissolve" not in inventory
    assert "player.rankings" in inventory
