"""Regression coverage for the V3 private-chat request contract."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_chat_send_suite_covers_both_addressing_forms_and_rejections():
    suite = json.loads((ROOT / "tests.v2/suite_communication.json").read_text())
    tests = {case["name"]: case for case in suite["tests"]}

    assert tests["chat.send - Send private message"]["data"] == {
        "to_player": "chat_test_user2",
        "message": "Hello player 2!",
    }
    assert tests["chat.send - Send private message by id"]["data"]["to_id"] == (
        "@{chat_user2_player_id}"
    )
    assert tests["chat.send - Reject multiple recipients"]["expect"]["status"] == "error"
    assert tests["chat.send - Reject missing recipient"]["expect"]["status"] == "error"
    assert tests["chat.send - Reject unknown field"]["expect"]["status"] == "error"

