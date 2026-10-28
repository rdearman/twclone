"""Public account registration is an explicit, secret-safe CLI flow."""

import sys

import client


def test_registration_flag_is_user_facing_and_not_debug_menu_only(menus, monkeypatch):
    assert "register" not in [item.get("action", {}).get("rpc", {}).get("command")
                              for item in menus["TESTING"]["options"]]
    monkeypatch.setattr(sys, "argv", ["client.py", "--register", "--user", "new-captain"])
    args = client.parse_args()
    assert args.register is True
    assert args.user == "new-captain"


def test_registration_payload_uses_public_protocol_fields():
    payload = client.registration_payload(
        "  captain  ", "never-print-this", " Scout ", "en-GB", "Europe/London")
    assert payload == {
        "username": "captain", "passwd": "never-print-this", "ship_name": "Scout",
        "ui_locale": "en-GB", "ui_timezone": "Europe/London",
    }


def test_registration_rejects_empty_fields_and_mismatched_confirmation(fake_conn):
    for args in [("", "secret"), ("captain", "")]:
        try:
            client.registration_payload(*args)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid registration fields were accepted")
    try:
        client.validate_registration_passwords("secret", "different")
    except ValueError as exc:
        assert "do not match" in str(exc)
    else:
        raise AssertionError("mismatched password confirmation was accepted")
    assert not fake_conn.calls


def test_registration_success_stores_session_and_new_account_can_log_in(fake_conn):
    fake_conn.queue("auth.register", {
        "status": "ok", "type": "auth.session",
        "data": {"player_id": 31, "session_token": "temporary-registration-session"},
    })
    response = client.register_account(fake_conn, "captain", "temporary-secret")
    assert response["status"] == "ok"
    assert fake_conn.session_token == "temporary-registration-session"
    assert fake_conn.calls == [{
        "command": "auth.register",
        "data": {"username": "captain", "passwd": "temporary-secret"},
    }]

    fresh_connection = fake_conn.__class__()
    fresh_connection.queue("auth.login", {
        "status": "ok", "type": "auth.session",
        "data": {"player_id": 31, "session_token": "temporary-login-session"},
    })
    login = client.authenticate(fresh_connection, "captain", "temporary-secret")
    assert login["status"] == "ok"
    assert fresh_connection.session_token == "temporary-login-session"


def test_duplicate_registration_is_reported_and_does_not_set_a_session(fake_conn):
    fake_conn.queue("auth.register", {
        "status": "error", "error": {"code": 1105, "message": "Username already exists"},
    })
    response = client.register_account(fake_conn, "captain", "temporary-secret")
    assert response["status"] == "error"
    assert fake_conn.session_token is None
    assert [call["command"] for call in fake_conn.calls] == ["auth.register"]
