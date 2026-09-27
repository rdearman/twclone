"""Python client tow flow uses the existing authoritative ship.tow command."""
import client


def _ctx(ctx_factory):
    ctx = ctx_factory(sector={
        "id": 2271,
        "ships": [
            {"id": 10, "name": "Player ship", "owner": "Newguy"},
            {"id": 20, "name": "Spare ship", "ship_type": "Scout", "owner": "Newguy"},
        ],
    })
    ctx.player_info = {"player": {"ship_id": 10}}
    return ctx


def test_tow_flow_engages_selected_ship_and_reports_server_confirmation(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = _ctx(ctx_factory)
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 10}}})
    fake_conn.queue("ship.tow", {
        "status": "ok", "type": "ship.tow.engaged",
        "data": {"status": "Towing beam engaged", "towee_ship_id": 20},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "20")

    client.tow_flow(ctx)

    assert fake_conn.calls[-1] == {"command": "ship.tow", "data": {"target_ship_id": 20}}
    assert ctx.state["towing_ship_id"] == 20
    assert "Tow beam engaged with ship 20" in capsys.readouterr().out


def test_tow_flow_releases_current_tow_with_empty_data(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = _ctx(ctx_factory)
    ctx.state["towing_ship_id"] = 20
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 10}}})
    fake_conn.queue("ship.tow", {
        "status": "ok", "type": "ship.tow.disengaged",
        "data": {"status": "Towing beam disengaged", "towee_ship_id": 20},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "r")

    client.tow_flow(ctx)

    assert fake_conn.calls[-1] == {"command": "ship.tow", "data": {}}
    assert "towing_ship_id" not in ctx.state
    assert "Tow beam released from ship 20" in capsys.readouterr().out


def test_tow_flow_cancels_without_sending_mutating_command(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = _ctx(ctx_factory)
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 10}}})
    monkeypatch.setattr("builtins.input", lambda *_: "")

    client.tow_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.status"]
    assert "cancelled" in capsys.readouterr().out.lower()


def test_tow_flow_rejects_ship_not_in_current_sector(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = _ctx(ctx_factory)
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 10}}})
    monkeypatch.setattr("builtins.input", lambda *_: "999")

    client.tow_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.status"]
    assert "not listed" in capsys.readouterr().out


def test_tow_flow_surfaces_authoritative_server_refusal(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = _ctx(ctx_factory)
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 10}}})
    fake_conn.queue("ship.tow", {
        "status": "refused", "error": {"message": "Target ship is currently piloted."},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "20")

    client.tow_flow(ctx)

    assert fake_conn.calls[-1]["command"] == "ship.tow"
    assert "Target ship is currently piloted" in capsys.readouterr().out


def test_has_tow_target_requires_real_non_player_ship_id():
    assert client.has_tow_target([{"id": 10}, {"name": "unknown"}], my_ship_id=10) is False
    assert client.has_tow_target([{"ship_id": 20}], my_ship_id=10) is True
