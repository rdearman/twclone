"""V3-HAZ-01: render the documented server hazard severity field."""

from presenters import present_move_hazards


def test_hazard_presentation_preserves_server_severity_without_inference():
    response = {
        "status": "ok",
        "data": {
            "hazards": [{
                "hazard_type": "radiation",
                "severity": "critical",
                "damage": 12,
            }],
        },
    }

    assert present_move_hazards(response) == [
        "Sector hazard · radiation · severity: critical · damage: 12",
    ]


def test_hazard_presentation_does_not_add_severity_when_server_omits_it():
    response = {
        "status": "ok",
        "data": {"hazards": [{"hazard_type": "nebula", "damage": 3}]},
    }

    assert present_move_hazards(response) == [
        "Sector hazard · nebula · damage: 3",
    ]
