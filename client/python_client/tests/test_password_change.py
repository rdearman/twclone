import json
import secrets
import socket

import client
from protocol import Conn


def _prompt_passwords(monkeypatch, values):
    iterator = iter(values)
    monkeypatch.setattr(client.getpass, "getpass", lambda _prompt: next(iterator))


def test_password_change_is_available_from_settings_menu(menus):
    option = next(
        item for item in menus["SETTINGS"]["options"]
        if item["action"].get("pycall") == "change_password_flow"
    )
    assert option["key"] == "c"
    assert "password" in option["label"].lower()


def test_password_change_sends_only_server_contract_fields(ctx_factory, fake_conn, monkeypatch, capsys):
    old_password, new_password = secrets.token_urlsafe(24), secrets.token_urlsafe(24)
    _prompt_passwords(monkeypatch, [old_password, new_password, new_password])
    fake_conn.queue("auth.change_password", {"status": "ok", "data": {"success": True}})

    client.change_password_flow(ctx_factory())

    assert fake_conn.calls == [{
        "command": "auth.change_password",
        "data": {"old_password": old_password, "new_password": new_password},
    }]
    output = capsys.readouterr().out
    assert "Password changed" in output
    assert old_password not in output and new_password not in output


def test_password_mismatch_and_empty_password_never_reach_server(ctx_factory, fake_conn, monkeypatch, capsys):
    values = [secrets.token_urlsafe(24), secrets.token_urlsafe(24), secrets.token_urlsafe(24)]
    _prompt_passwords(monkeypatch, values)
    client.change_password_flow(ctx_factory())
    mismatch_output = capsys.readouterr().out
    assert "do not match" in mismatch_output
    assert not fake_conn.calls
    assert all(value not in mismatch_output for value in values)

    _prompt_passwords(monkeypatch, ["", "", ""])
    client.change_password_flow(ctx_factory())
    assert "cannot be empty" in capsys.readouterr().out
    assert not fake_conn.calls


def test_wrong_current_password_is_reported_without_echo(ctx_factory, fake_conn, monkeypatch, capsys):
    old_password, new_password = secrets.token_urlsafe(24), secrets.token_urlsafe(24)
    _prompt_passwords(monkeypatch, [old_password, new_password, new_password])
    fake_conn.queue("auth.change_password", {
        "status": "error", "error": {"code": 1101, "message": "Invalid old password"},
    })

    client.change_password_flow(ctx_factory())

    output = capsys.readouterr().out
    assert "current password was not accepted" in output
    assert old_password not in output and new_password not in output


def test_rejected_password_change_does_not_claim_success(ctx_factory, fake_conn, monkeypatch, capsys):
    old_password, new_password = secrets.token_urlsafe(24), secrets.token_urlsafe(24)
    _prompt_passwords(monkeypatch, [old_password, new_password, new_password])
    fake_conn.queue("auth.change_password", {
        "status": "refused", "error": {"code": 1401, "message": "Rejected"},
    })

    client.change_password_flow(ctx_factory())

    output = capsys.readouterr().out
    assert "refused" in output and "Password changed" not in output
    assert old_password not in output and new_password not in output


def test_new_password_can_authenticate_on_a_fresh_connection(fake_conn):
    new_password = secrets.token_urlsafe(24)
    fresh_connection = fake_conn.__class__()
    fresh_connection.queue("auth.login", {
        "status": "error", "error": {"code": 1101, "message": "Invalid login"},
    })
    fresh_connection.queue("auth.login", {
        "status": "ok", "data": {"session_token": "ephemeral-session-token"},
    })

    old_response = client.authenticate(fresh_connection, "qa-player", secrets.token_urlsafe(24))
    response = client.authenticate(fresh_connection, "qa-player", new_password)

    assert old_response["status"] == "error"
    assert response["status"] == "ok"
    assert [call["command"] for call in fresh_connection.calls] == ["auth.login", "auth.login"]
    assert fresh_connection.calls[1]["data"]["passwd"] == new_password
    assert fresh_connection.session_token == "ephemeral-session-token"


def test_debug_protocol_logs_redact_passwords_and_session_tokens(capsys):
    local_socket, peer = socket.socketpair()
    conn = Conn(local_socket, debug=True)
    old_password, new_password, session_token = (secrets.token_urlsafe(24) for _ in range(3))
    try:
        conn.session_token = session_token
        conn.send({"command": "auth.change_password", "data": {
            "old_password": old_password,
            "new_password": new_password,
            "confirm_password": new_password,
        }})
        sent = peer.recv(4096)
        assert old_password.encode() in sent and new_password.encode() in sent
        output = capsys.readouterr().out
        assert old_password not in output and new_password not in output and session_token not in output
        assert output.count("<redacted>") == 4

        peer.sendall(json.dumps({"status": "error", "data": {"session_token": session_token}}).encode() + b"\n")
        conn.recv()
        output = capsys.readouterr().out
        assert session_token not in output and "<redacted>" in output
    finally:
        conn._r.close()
        local_socket.close()
        peer.close()
