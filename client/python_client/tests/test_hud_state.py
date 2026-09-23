"""
Tests for hud.py — authoritative HUD field extraction/hydration and the
connection-state derivation used by the persistent HUD.

Requirements covered:
6. Login hydration populates every available HUD field.
7. Unknown fields remain unknown rather than becoming zero.
8. Refused/error responses do not mutate HUD state.
9. Successful state-changing responses update relevant fields.
"""
from hud import (
    HudState, hydrate_login, apply_response, merge_hud,
    extract_player_fields, extract_ship_fields, extract_mail_unread,
    extract_notice_unread, extract_login_unread_news, connection_label,
)

LOGIN_RESP = {"status": "ok", "data": {"unread_news_count": 3, "session_token": "tok"}}
MY_INFO_RESP = {"status": "ok", "data": {"player": {"turns_remaining": 317, "credits": "12450.00"}}}
SHIP_RESP = {
    "status": "ok",
    "data": {"ship": {
        "id": 7, "name": "Wayfarer", "fighters": 250, "shields": 80,
        "holds": 40, "cargo": [{"commodity": "ORE", "qty": 10}, {"commodity": "EQUIPMENT", "qty": 8}],
    }},
}
MAIL_RESP = {
    "status": "ok",
    "data": {"items": [
        {"id": 1, "read_at": "2024-01-01T00:00:00Z"},
        {"id": 2},
        {"id": 3},
    ]},
}
NOTICE_RESP = {
    "status": "ok",
    "data": {"items": [
        {"id": 1, "seen_at": 123},
        {"id": 2},
    ]},
}


def test_login_hydration_populates_every_available_field():
    hud = HudState()
    hud = hydrate_login(
        hud, LOGIN_RESP,
        my_info_resp=MY_INFO_RESP, ship_resp=SHIP_RESP,
        sector_resp={"id": 42, "name": "Rigel"},
        mail_resp=MAIL_RESP, notice_resp=NOTICE_RESP,
    )
    assert hud.turns_remaining == 317
    assert hud.credits == 12450
    assert hud.ship_id == 7
    assert hud.ship_name == "Wayfarer"
    assert hud.fighters == 250
    assert hud.shields == 80
    assert hud.cargo_total == 40
    assert hud.cargo_used == 18
    assert hud.sector_id == 42
    assert hud.sector_name == "Rigel"
    assert hud.unread_mail == 2
    assert hud.unread_notices == 1
    assert hud.unread_news == 3
    assert hud.last_refresh_ts is not None


def test_unknown_fields_remain_unknown_not_zero():
    hud = HudState()
    # No responses supplied at all -> every optional field stays None, not 0.
    hud = hydrate_login(hud, {"status": "ok", "data": {}})
    assert hud.turns_remaining is None
    assert hud.credits is None
    assert hud.fighters is None
    assert hud.shields is None
    assert hud.cargo_used is None
    assert hud.cargo_total is None
    assert hud.unread_mail is None
    assert hud.unread_notices is None
    assert hud.unread_news is None


def test_zero_is_a_valid_distinct_value_from_unknown():
    ship_zero_cargo = {"status": "ok", "data": {"ship": {"holds": 40, "cargo": []}}}
    fields = extract_ship_fields(ship_zero_cargo)
    assert fields["cargo_used"] == 0
    assert fields["cargo_used"] is not None


def test_refused_response_does_not_mutate_hud_state():
    hud = HudState(turns_remaining=317, credits=12450)
    refused = {"status": "refused", "error": {"code": "INSUFFICIENT_TURNS", "message": "no turns"}}
    new_hud = apply_response(hud, "player.my_info", refused)
    assert new_hud == hud
    assert new_hud.turns_remaining == 317
    assert new_hud.credits == 12450


def test_error_response_does_not_mutate_hud_state():
    hud = HudState(fighters=250)
    error = {"status": "error", "error": {"code": 500, "message": "boom"}}
    new_hud = apply_response(hud, "ship.status", error)
    assert new_hud == hud
    assert new_hud.fighters == 250


def test_successful_response_updates_relevant_fields_via_apply_response():
    hud = HudState()
    new_hud = apply_response(hud, "player.my_info", MY_INFO_RESP)
    assert new_hud.turns_remaining == 317
    assert new_hud.credits == 12450
    assert new_hud.last_refresh_ts is not None

    new_hud2 = apply_response(new_hud, "ship.status", SHIP_RESP)
    assert new_hud2.fighters == 250
    assert new_hud2.cargo_used == 18
    # Fields not touched by ship.status are preserved from the prior merge.
    assert new_hud2.turns_remaining == 317


def test_apply_response_is_a_no_op_for_unmapped_commands():
    hud = HudState(turns_remaining=5)
    new_hud = apply_response(hud, "move.warp", {"status": "ok", "data": {"sector_id": 2}})
    assert new_hud == hud


def test_malformed_credits_leaves_credits_unknown_not_a_traceback():
    resp = {"status": "ok", "data": {"player": {"credits": "not-a-number", "turns_remaining": 5}}}
    fields = extract_player_fields(resp)
    assert "credits" not in fields
    assert fields["turns_remaining"] == 5


def test_connection_label_online_stale_offline():
    assert connection_label(connected=False, last_refresh_ts=None) == "OFFLINE"
    assert connection_label(connected=True, last_refresh_ts=None) == "ONLINE"
    assert connection_label(connected=True, last_refresh_ts=100.0, now=110.0) == "ONLINE"
    assert connection_label(connected=True, last_refresh_ts=100.0, now=200.0) == "STALE"
