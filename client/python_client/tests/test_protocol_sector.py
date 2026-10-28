from protocol import extract_current_sector


def test_extract_current_sector_uses_authoritative_player_sector():
    response = {
        "status": "ok",
        "type": "player.info",
        "data": {"player": {"id": 12, "sector": 7}},
    }

    assert extract_current_sector(response) == 7


def test_extract_current_sector_accepts_player_sector_id_alias():
    response = {"status": "ok", "data": {"player": {"sector_id": 9}}}

    assert extract_current_sector(response) == 9
