"""V3-SHIP-02 client acceptance for the authoritative ship roster."""

import client


def test_owned_ship_roster_inspection_uses_authoritative_ids(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [
            {"ship_id": 7, "name": "Hauler", "active": True},
            {"ship_id": 9, "name": "Scout", "active": False},
        ]},
    })
    fake_conn.queue("ship.inspect", {
        "status": "ok", "type": "ship.inspect",
        "data": {"ship": {"ship_id": 9, "name": "Scout", "sector_id": 14}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "2")

    client.ship_roster_inspect_flow(ctx)

    assert fake_conn.calls == [
        {"command": "ship.list", "data": {}},
        {"command": "ship.inspect", "data": {"ship_id": 9}},
    ]
    output = capsys.readouterr().out
    assert "Server-authoritative ship inspection" in output
    assert "Scout" in output


def test_owned_ship_roster_inspection_rejects_mismatched_response(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 7, "name": "Hauler", "active": True}]},
    })
    fake_conn.queue("ship.inspect", {
        "status": "ok", "type": "ship.inspect",
        "data": {"ship": {"ship_id": 99, "name": "Other ship"}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "1")

    client.ship_roster_inspect_flow(ctx)

    output = capsys.readouterr().out
    assert "different ship ID" in output
    assert "Other ship" not in output


def test_owned_ship_inspection_disconnect_reconciles_authoritative_ship(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 7, "name": "Hauler", "active": True}]},
    })
    original_rpc = fake_conn.rpc

    def disconnect_during_inspection(command, data):
        if command == "ship.inspect":
            raise ConnectionError("server disconnected")
        return original_rpc(command, data)

    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 7, "name": "Confirmed"}},
    })
    monkeypatch.setattr(fake_conn, "rpc", disconnect_during_inspection)
    monkeypatch.setattr("builtins.input", lambda *_: "1")

    client.ship_roster_inspect_flow(ctx)

    output = capsys.readouterr().out
    assert "outcome is uncertain after connection loss" in output
    assert "Server-authoritative ship inspection" not in output
    assert [call["command"] for call in fake_conn.calls] == [
        "ship.list", "ship.inspect", "ship.status"
    ]


def test_ship_transfer_requires_typed_matching_receipt_and_refreshes_roster(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.state["player_id"] = 11
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 7, "name": "Still owned", "active": True}]},
    })
    fake_conn.queue("ship.transfer", {
        "status": "ok", "type": "ship.transfer.response",
        "data": {"ship_id": 7, "from_player_id": 11, "target_player_id": 42},
    })
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 9, "name": "Scout", "active": True}]},
    })
    fake_conn.queue("player.my_info", {"status": "ok", "type": "player.info", "data": {"player_id": 11}})
    fake_conn.queue("ship.status", {"status": "ok", "type": "ship.status", "data": {"ship": {"id": 7}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 14, "ships": []}})
    answers = iter(["7", "42", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.ship_transfer_flow(ctx)

    assert fake_conn.calls == [
        {"command": "ship.list", "data": {}},
        {"command": "ship.transfer", "data": {"ship_id": 7, "target_player_id": 42}},
        {"command": "player.my_info", "data": {}},
        {"command": "ship.status", "data": {}},
        {"command": "move.describe_sector", "data": {}},
        {"command": "ship.list", "data": {}},
    ]
    output = capsys.readouterr().out
    assert "transfer confirmed by server" in output
    assert "9: Scout" in output
    assert ctx.player_info == {"player_id": 11}
    assert ctx.last_sector_desc["id"] == 14


def test_ship_transfer_rejects_mismatched_receipt_without_claiming_success(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.state["player_id"] = 11
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 7, "name": "Still owned", "active": True}]},
    })
    fake_conn.queue("ship.transfer", {
        "status": "ok", "type": "ship.transfer.response",
        "data": {"ship_id": 99, "from_player_id": 11, "target_player_id": 42},
    })
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 7, "name": "Still owned", "active": True}]},
    })
    answers = iter(["7", "42", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.ship_transfer_flow(ctx)

    output = capsys.readouterr().out
    assert "was not confirmed" in output
    assert "transfer confirmed by server" not in output
    assert [call["command"] for call in fake_conn.calls] == ["ship.list", "ship.transfer", "ship.list"]


def test_ship_transfer_rejects_receipt_from_different_authenticated_owner(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.state["player_id"] = 11
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 7, "name": "Still owned", "active": True}]},
    })
    fake_conn.queue("ship.transfer", {
        "status": "ok", "type": "ship.transfer.response",
        "data": {"ship_id": 7, "from_player_id": 99, "target_player_id": 42},
    })
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 7, "name": "Still owned", "active": True}]},
    })
    answers = iter(["1", "42", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.ship_transfer_flow(ctx)

    output = capsys.readouterr().out
    assert "was not confirmed" in output
    assert "transfer confirmed by server" not in output


def test_ship_roster_renders_server_owned_rows(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [
            {"ship_id": 7, "name": "Hauler", "sector_id": 12, "active": True},
            {"ship_id": 9, "name": "Scout", "sector_id": 14, "active": False},
        ]},
    })

    client.ship_list_flow(ctx)

    assert fake_conn.calls == [{"command": "ship.list", "data": {}}]
    output = capsys.readouterr().out
    assert "7: Hauler" in output and "active" in output
    assert "9: Scout" in output and "inactive" in output


def test_ship_roster_rejects_malformed_authoritative_id(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 7.0, "name": "Untrusted"}]},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 7, "name": "Confirmed"}},
    })

    client.ship_list_flow(ctx)

    output = capsys.readouterr().out
    assert "Untrusted" not in output
    assert "No authoritative ships were returned" in output
    assert fake_conn.calls[-1] == {"command": "ship.status", "data": {}}


def test_ship_roster_refresh_disconnect_does_not_claim_empty_roster(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response", "data": {"ships": []},
    })
    original_rpc = fake_conn.rpc

    def disconnect_on_ship_refresh(command, data):
        if command == "ship.status":
            raise ConnectionError("server disconnected")
        return original_rpc(command, data)

    monkeypatch.setattr(fake_conn, "rpc", disconnect_on_ship_refresh)

    client.ship_list_flow(ctx)

    output = capsys.readouterr().out
    assert "No authoritative ships were returned" in output
    assert "could not be confirmed after connection loss" in output


def test_ship_roster_rejects_duplicate_but_preserves_server_active_rows(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [
            {"ship_id": 7, "name": "Hauler", "active": True},
            {"ship_id": 7, "name": "Duplicate", "active": False},
        ]},
    })
    client.ship_list_flow(ctx)

    output = capsys.readouterr().out
    assert "ambiguous ownership or active-ship state" in output
    assert "Duplicate" not in output

    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [
            {"ship_id": 7, "name": "Hauler", "active": True},
            {"ship_id": 9, "name": "Scout", "active": True},
        ]},
    })
    client.ship_list_flow(ctx)
    assert "Scout" in capsys.readouterr().out


def test_ship_roster_displays_authoritative_empty_array(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response", "data": {"ships": []},
    })

    client.ship_list_flow(ctx)

    assert fake_conn.calls == [{"command": "ship.list", "data": {}}]
    assert "No ships are currently owned." in capsys.readouterr().out


def test_ship_roster_refusal_refreshes_authoritative_active_ship(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "refused",
        "error": {"message": "Ship roster is unavailable."},
    })
    fake_conn.queue("ship.status", {
        "status": "ok",
        "type": "ship.status",
        "data": {"ship": {"id": 7, "active": True}},
    })

    client.ship_list_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.list", "ship.status"]
    assert "Ship roster is unavailable." in capsys.readouterr().out


def test_ship_roster_unexpected_type_refreshes_authoritative_active_ship(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "ok",
        "type": "ship.status",
        "data": {"ships": [{"ship_id": 7, "active": True}]},
    })
    fake_conn.queue("ship.status", {
        "status": "ok",
        "type": "ship.status",
        "data": {"ship": {"id": 7, "active": True}},
    })

    client.ship_list_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.list", "ship.status"]
    assert "unexpected response type" in capsys.readouterr().out


def test_ship_roster_requires_canonical_response_type(ctx_factory, fake_conn, capsys):
    """A missing schema identity must not authorize an ownership roster."""
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "ok",
        "data": {"ships": [{"ship_id": 7, "name": "Untrusted", "active": True}]},
    })
    fake_conn.queue("ship.status", {
        "status": "ok",
        "type": "ship.status",
        "data": {"ship": {"id": 4, "name": "Confirmed"}},
    })

    client.ship_list_flow(ctx)

    output = capsys.readouterr().out
    assert "Untrusted" not in output
    assert "unexpected response type" in output
    assert fake_conn.calls == [
        {"command": "ship.list", "data": {}},
        {"command": "ship.status", "data": {}},
    ]


def test_ship_roster_refresh_rejects_wrong_typed_ship_status(ctx_factory, fake_conn, capsys):
    """A recovery read must not hydrate HUD state from another response type."""
    ctx = ctx_factory()
    ctx.hud = ctx.hud.__class__(ship_id=4)
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": []},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "player.info",
        "data": {"ship": {"id": 99, "name": "Untrusted"}},
    })

    client.ship_list_flow(ctx)

    output = capsys.readouterr().out
    assert "Untrusted" not in output
    assert "unexpected response type" in output
    assert ctx.hud.ship_id == 4
    assert fake_conn.calls == [
        {"command": "ship.list", "data": {}},
        {"command": "ship.status", "data": {}},
    ]

