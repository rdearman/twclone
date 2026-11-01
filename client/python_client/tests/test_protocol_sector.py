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


def test_extract_current_sector_rejects_boolean_or_nonpositive_ids():
    assert extract_current_sector({"data": {"current_sector": True, "sector_id": 9}}) is None
    assert extract_current_sector({"data": {"current_sector": 0, "sector_id": 9}}) is None
    assert extract_current_sector({"data": {"player": {"sector": False, "sector_id": 9}}}) is None


def test_extract_current_sector_accepts_only_positive_integer_location_ids():
    assert extract_current_sector({"data": {"ship": {"location": {"sector_id": 11}}}}) == 11
    assert extract_current_sector({"data": {"ship": {"location": {"sector_id": 11.0}}}}) is None
