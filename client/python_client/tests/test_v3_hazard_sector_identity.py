"""V3-HAZ-01: retain the server-reported sector identity in hazard output."""

from presenters import present_move_hazards


def test_hazard_report_preserves_authoritative_sector_id():
    response = {
        "status": "error",
        "error": {
            "data": {
                "sector_id": 44,
                "ship_destroyed": True,
                "hazards": [{"hazard_type": "minefield", "hull_lost": 100}],
            }
        },
    }

    assert present_move_hazards(response) == [
        "Sector hazard · minefield · hull lost: 100",
        "Movement outcome · reported sector: 44",
        "Movement outcome · ship destroyed: True",
    ]
