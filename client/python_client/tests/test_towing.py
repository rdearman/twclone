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
    fake_conn.queue("ship.tow.status", {"status": "ok", "data": {"ship_id": 10, "towing_ship_id": None, "towed_by_ship_id": None}})
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
    fake_conn.queue("ship.tow.status", {"status": "ok", "data": {"ship_id": 10, "towing_ship_id": 20, "towed_by_ship_id": None}})
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
    fake_conn.queue("ship.tow.status", {"status": "ok", "data": {"ship_id": 10, "towing_ship_id": None, "towed_by_ship_id": None}})
    monkeypatch.setattr("builtins.input", lambda *_: "")

    client.tow_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.tow.status"]
    assert "cancelled" in capsys.readouterr().out.lower()


def test_tow_flow_rejects_ship_not_in_current_sector(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = _ctx(ctx_factory)
    fake_conn.queue("ship.tow.status", {"status": "ok", "data": {"ship_id": 10, "towing_ship_id": None, "towed_by_ship_id": None}})
    monkeypatch.setattr("builtins.input", lambda *_: "999")

    client.tow_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.tow.status"]
    assert "not listed" in capsys.readouterr().out


def test_tow_flow_surfaces_authoritative_server_refusal(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = _ctx(ctx_factory)
    fake_conn.queue("ship.tow.status", {"status": "ok", "data": {"ship_id": 10, "towing_ship_id": None, "towed_by_ship_id": None}})
    fake_conn.queue("ship.tow", {
        "status": "refused", "error": {"message": "Target ship is currently piloted."},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "20")

    client.tow_flow(ctx)

    assert fake_conn.calls[-1]["command"] == "ship.tow"
    assert "Target ship is currently piloted" in capsys.readouterr().out


def test_tow_status_reconciles_existing_tow_after_reconnect(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = _ctx(ctx_factory)
    fake_conn.queue("ship.tow.status", {"status": "ok", "data": {
        "ship_id": 10, "towing_ship_id": 20, "towed_by_ship_id": None,
    }})
    fake_conn.queue("ship.tow", {
        "status": "ok", "type": "ship.tow.disengaged",
        "data": {"status": "Towing beam disengaged", "towee_ship_id": 20},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "r")

    client.tow_flow(ctx)

    assert fake_conn.calls[0] == {"command": "ship.tow.status", "data": {}}
    assert "Currently towing ship 20" in capsys.readouterr().out
    assert "towing_ship_id" not in ctx.state


def test_has_tow_target_requires_real_non_player_ship_id():
    assert client.has_tow_target([{"id": 10}, {"name": "unknown"}], my_ship_id=10) is False
    assert client.has_tow_target([{"ship_id": 20}], my_ship_id=10) is True
    assert client.has_tow_target([{"ship_id": True}], my_ship_id=10) is False


def test_tow_flow_does_not_submit_boolean_target_id(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={
        "id": 2271,
        "ships": [
            {"id": 10, "name": "Player ship", "owner": "Newguy"},
            {"ship_id": True, "name": "Untrusted target", "owner": "Newguy"},
        ],
    })
    ctx.player_info = {"player": {"ship_id": 10}}
    fake_conn.queue("ship.tow.status", {
        "status": "ok", "data": {"ship_id": 10, "towing_ship_id": None},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "1")

    client.tow_flow(ctx)

    assert fake_conn.calls == [{"command": "ship.tow.status", "data": {}}]
    assert "not listed" in capsys.readouterr().out


def test_tow_status_boolean_id_is_not_treated_as_active_tow(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = _ctx(ctx_factory)
    fake_conn.queue("ship.tow.status", {
        "status": "ok", "data": {"ship_id": 10, "towing_ship_id": True},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "20")

    client.tow_flow(ctx)

    assert fake_conn.calls == [{"command": "ship.tow.status", "data": {}}]
    assert "Cannot engage a tow until the server confirms" in capsys.readouterr().out


def test_selecting_current_tow_does_not_toggle_it_off(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = _ctx(ctx_factory)
    fake_conn.queue("ship.tow.status", {"status": "ok", "data": {"towing_ship_id": 20}})
    monkeypatch.setattr("builtins.input", lambda *_: "20")

    client.tow_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.tow.status"]
    assert ctx.state["towing_ship_id"] == 20
    assert "Enter R to release" in capsys.readouterr().out


def test_unknown_tow_status_does_not_risk_toggling_selected_ship(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = _ctx(ctx_factory)
    fake_conn.queue("ship.tow.status", {"status": "error", "error": {"message": "Unavailable"}})
    monkeypatch.setattr("builtins.input", lambda *_: "20")

    client.tow_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.tow.status"]
    assert "until the server confirms" in capsys.readouterr().out


def test_tow_hazard_is_presented_and_authoritative_state_is_refreshed(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = _ctx(ctx_factory)
    fake_conn.queue("ship.tow.status", {"status": "ok", "data": {
        "ship_id": 10, "towing_ship_id": None,
    }})
    fake_conn.queue("ship.tow", {
        "status": "ok", "type": "ship.tow.engaged",
        "data": {"status": "Towing beam engaged", "towee_ship_id": 20,
                  "hazards": [{"hazard_type": "minefield", "hull_lost": 6}]},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"sector_id": 2271, "ships": []},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns_remaining": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 10, "hull": 94}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "20")

    client.tow_flow(ctx)

    output = capsys.readouterr().out
    assert "minefield" in output and "hull lost: 6" in output
    assert [call["command"] for call in fake_conn.calls] == [
        "ship.tow.status", "ship.tow", "move.describe_sector",
        "player.my_info", "ship.status",
    ]


def test_tow_hazard_refusal_refreshes_without_recording_tow(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = _ctx(ctx_factory)
    fake_conn.queue("ship.tow.status", {"status": "ok", "data": {
        "ship_id": 10, "towing_ship_id": None,
    }})
    fake_conn.queue("ship.tow", {
        "status": "refused", "type": "error",
        "error": {"message": "Entry hazard stopped towing", "data": {
            "hazards": [{"hazard_type": "limpet", "damage": 4}],
        }},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"sector_id": 2271, "ships": []},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns_remaining": 40}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 10, "hull": 96}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "20")

    client.tow_flow(ctx)

    output = capsys.readouterr().out
    assert "limpet" in output and "damage: 4" in output
    assert "towing_ship_id" not in ctx.state
    assert [call["command"] for call in fake_conn.calls] == [
        "ship.tow.status", "ship.tow", "move.describe_sector",
        "player.my_info", "ship.status",
    ]


def test_tow_engagement_does_not_infer_missing_authoritative_towee_id(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = _ctx(ctx_factory)
    fake_conn.queue("ship.tow.status", {"status": "ok", "data": {
        "ship_id": 10, "towing_ship_id": None,
    }})
    fake_conn.queue("ship.tow", {
        "status": "ok", "type": "ship.tow.engaged",
        "data": {"status": "Towing beam engaged"},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "20")

    client.tow_flow(ctx)

    assert "towing_ship_id" not in ctx.state
    assert "authoritative towed ship ID" in capsys.readouterr().out


def test_tow_success_without_canonical_response_type_is_reconciled(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """An untyped success must not authorize a local tow relationship."""
    ctx = _ctx(ctx_factory)
    fake_conn.queue("ship.tow.status", {"status": "ok", "data": {
        "ship_id": 10, "towing_ship_id": None,
    }})
    fake_conn.queue("ship.tow", {
        "status": "ok", "data": {
            "status": "Towing beam engaged", "towee_ship_id": 20,
            "hazards": [{"hazard_type": "minefield", "hull_lost": 6}],
        },
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"sector_id": 2271, "ships": []},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns_remaining": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 10, "hull": 94}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "20")

    client.tow_flow(ctx)

    output = capsys.readouterr().out
    assert "unexpected type" in output
    assert "minefield" not in output
    assert "towing_ship_id" not in ctx.state
    assert [call["command"] for call in fake_conn.calls] == [
        "ship.tow.status", "ship.tow", "move.describe_sector",
        "player.my_info", "ship.status",
    ]
