"""V3-HAZ-01 client rendering for authoritative terminal hazard outcomes."""

import client


def test_top_level_ship_destroyed_is_rendered_without_inference():
    lines = client.present_move_hazards({
        "status": "error",
        "error": {"data": {"ship_destroyed": True}},
    })

    assert lines == ["Movement outcome · ship destroyed: True"]


def test_missing_terminal_state_is_not_invented():
    lines = client.present_move_hazards({
        "status": "ok",
        "data": {"hazards": [{"hazard_type": "nebula", "hull_lost": 4}]},
    })

    assert lines == ["Sector hazard · nebula · hull lost: 4"]


def test_nonempty_hazards_preserve_terminal_state_from_error_payload():
    """Keep independently supplied authoritative fields from both envelopes."""
    lines = client.present_move_hazards({
        "status": "error",
        "data": {"hazards": [{"hazard_type": "minefield", "hull_lost": 12}]},
        "error": {"data": {"ship_destroyed": True}},
    })

    assert lines == [
        "Sector hazard · minefield · hull lost: 12",
        "Movement outcome · ship destroyed: True",
    ]


def test_entry_refusal_preserves_authoritative_ship_location_outcome():
    lines = client.present_move_hazards({
        "status": "refused",
        "error": {"data": {
            "ship_remains_in_sector": False,
            "hazards": [{"hazard_type": "minefield", "hull_lost": 100}],
        }},
    })

    assert lines == [
        "Sector hazard · minefield · hull lost: 100",
        "Movement outcome · ship remains in sector: False",
    ]


def test_terminal_outcomes_merge_without_overriding_explicit_data():
    lines = client.present_move_hazards({
        "status": "error",
        "data": {"ship_destroyed": False},
        "error": {"data": {"ship_remains_in_sector": False}},
    })

    assert lines == [
        "Movement outcome · ship destroyed: False",
        "Movement outcome · ship remains in sector: False",
    ]


def test_malformed_success_hazards_do_not_hide_error_report():
    lines = client.present_move_hazards({
        "status": "error",
        "data": {"hazards": {"unexpected": "shape"}},
        "error": {"data": {"hazards": [{"hazard_type": "limpet", "damage": 6}]}},
    })

    assert lines == ["Sector hazard · limpet · damage: 6"]


def test_hazard_without_authoritative_identity_is_not_invented():
    lines = client.present_move_hazards({
        "status": "ok",
        "data": {"hazards": [{"damage": 6}, {"hazard_type": 17, "hull_lost": 4}]},
    })

    assert lines == []


def test_malformed_terminal_fields_are_not_presented_as_authoritative():
    lines = client.present_move_hazards({
        "status": "error",
        "data": {
            "sector_id": "44",
            "ship_destroyed": "false",
            "hazards": [{
                "hazard_type": "minefield",
                "hull_lost": "12",
                "destroyed": 1,
                "message": 99,
            }],
        },
    })

    assert lines == ["Sector hazard · minefield"]


def test_fractional_counter_is_not_presented_as_authoritative():
    lines = client.present_move_hazards({
        "status": "ok",
        "data": {"hazards": [{"hazard_type": "minefield", "hull_lost": 12.0}]},
    })

    assert lines == ["Sector hazard · minefield"]


def test_valid_terminal_fields_remain_presented():
    lines = client.present_move_hazards({
        "status": "error",
        "data": {
            "sector_id": 44,
            "ship_destroyed": True,
            "hazards": [{"hazard_type": "minefield", "hull_lost": 12}],
        },
    })

    assert lines == [
        "Sector hazard · minefield · hull lost: 12",
        "Movement outcome · reported sector: 44",
        "Movement outcome · ship destroyed: True",
    ]
