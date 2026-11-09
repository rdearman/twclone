"""V3-HAZ-01: explicit terminal state may be attached to a hazard row."""

import client


def test_hazard_row_destroyed_is_terminal_without_damage_inference():
    assert client._move_response_is_terminal({
        "status": "error",
        "data": {"hazards": [{"hazard_type": "minefield", "hull_lost": 100,
                                "destroyed": True}]},
    }) is True


def test_hazard_row_remaining_flag_is_terminal_without_destroyed_flag():
    assert client._move_response_is_terminal({
        "status": "error",
        "error": {"data": {"hazards": [
            {"hazard_type": "limpet", "damage": 4,
             "ship_remains_in_sector": False},
        ]}},
    }) is True


def test_hazard_damage_alone_is_not_terminal():
    assert client._move_response_is_terminal({
        "status": "error",
        "data": {"hazards": [{"hazard_type": "nebula", "hull_lost": 4}]},
    }) is False
