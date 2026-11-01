"""Ship and StarDock flows use only server-listed hardware and hull contracts."""
from dataclasses import replace

import client


def test_selected_ship_inspection_uses_authoritative_id_and_response(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 7, "ships": [{"id": 88, "name": "Hauler"}]})
    ctx.player_info = {"player": {"ship_id": 1, "name": "Pilot"}}
    fake_conn.queue("ship.inspect", {"status": "ok", "type": "ship.inspect", "data": {
        "ship": {"ship_id": 88, "name": "Confirmed Hauler", "sector_id": 7},
    }})
    answers = iter(["1", "i", "q"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.enter_ship_menu(ctx)

    assert fake_conn.calls == [{"command": "ship.inspect", "data": {"ship_id": 88}}]
    output = capsys.readouterr().out
    assert "Server-authoritative ship inspection" in output
    assert "Confirmed Hauler" in output
    assert ctx.state["last_rpc"]["status"] == "ok"


def test_selected_ship_inspection_refusal_does_not_claim_cached_details(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 7, "ships": [{"id": 88, "name": "Cached Hauler"}]})
    ctx.player_info = {"player": {"ship_id": 1, "name": "Pilot"}}
    fake_conn.queue("ship.inspect", {"status": "refused", "error": {
        "message": "Ship not found.",
    }})
    answers = iter(["1", "i", "q"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.enter_ship_menu(ctx)

    assert fake_conn.calls == [{"command": "ship.inspect", "data": {"ship_id": 88}}]
    output = capsys.readouterr().out
    assert "Ship inspection unavailable: Ship not found." in output
    assert "Server-authoritative ship inspection" not in output


def test_selected_ship_inspection_rejects_untyped_success(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 7, "ships": [{"id": 88, "name": "Visible"}]})
    ctx.player_info = {"player": {"ship_id": 1, "name": "Pilot"}}
    fake_conn.queue("ship.inspect", {"status": "ok", "data": {
        "ship": {"ship_id": 88, "name": "Untrusted"},
    }})
    # The menu exits after the inspection command returns.
    inputs = iter(["i", "q"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(inputs))

    client.enter_ship_menu(ctx)

    output = capsys.readouterr().out
    assert "unexpected response type" in output
    assert "Server-authoritative ship inspection" not in output


def test_selected_ship_inspection_rejects_mismatched_response_id(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 7, "ships": [{"id": 88, "name": "Cached Hauler"}]})
    ctx.player_info = {"player": {"ship_id": 1, "name": "Pilot"}}
    fake_conn.queue("ship.inspect", {"status": "ok", "type": "ship.inspect", "data": {
        "ship": {"ship_id": 99, "name": "Unselected Hauler", "sector_id": 7},
    }})
    answers = iter(["1", "i", "q"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.enter_ship_menu(ctx)

    output = capsys.readouterr().out
    assert "different ship ID" in output
    assert "Unselected Hauler" not in output


def test_selected_ship_without_id_is_not_inspected(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 7, "ships": [{"name": "Unidentified"}]})
    ctx.player_info = {"player": {"ship_id": 1, "name": "Pilot"}}
    answers = iter(["1", "i", "q"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.enter_ship_menu(ctx)

    assert fake_conn.calls == []
    assert "No boardable ships here." in capsys.readouterr().out


def test_contradictory_ship_ids_are_not_used_for_ownership_actions(
    ctx_factory, fake_conn, capsys
):
    """A visible row with conflicting ID aliases cannot authorize a claim."""
    ctx = ctx_factory(sector={"id": 7, "ships": [{
        "id": 88, "ship_id": 99, "name": "Ambiguous", "owner": "derelict",
    }]})
    ctx.player_info = {"player": {"ship_id": 1, "name": "Pilot"}}

    client.enter_ship_menu(ctx)

    assert fake_conn.calls == []
    output = capsys.readouterr().out
    assert "No boardable ships here." in output
    assert "Ambiguous" not in output


def test_selected_ship_sale_is_visible_but_unavailable_without_server_handler(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 7, "ships": [{"id": 88, "name": "Hauler"}]})
    ctx.player_info = {"player": {"ship_id": 1, "name": "Pilot"}}
    answers = iter(["1", "v", "y", "q"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.enter_ship_menu(ctx)

    assert fake_conn.calls == []
    assert "Ship sale unavailable" in capsys.readouterr().out


def test_selected_ship_transfer_uses_authoritative_receipt_and_refreshes(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 7, "ships": [{"id": 88, "name": "Hauler"}]})
    ctx.player_info = {"player": {"ship_id": 1, "name": "Pilot"}}
    ctx.state["player_id"] = 1
    fake_conn.queue("ship.transfer", {
        "status": "ok", "type": "ship.transfer.response",
        "data": {"ship_id": 88, "from_player_id": 1, "target_player_id": 42},
    })
    fake_conn.queue("player.my_info", {"status": "ok", "type": "player.info", "data": {"player_id": 1}})
    fake_conn.queue("ship.status", {"status": "ok", "type": "ship.status", "data": {"ship": {"id": 1}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "type": "sector.info", "data": {"id": 7, "ships": []}})
    fake_conn.queue("ship.list", {"status": "ok", "type": "ship.list.response", "data": {"ships": []}})
    answers = iter(["1", "t", "y", "42", "q"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.enter_ship_menu(ctx)

    assert fake_conn.calls[0] == {"command": "ship.transfer", "data": {"ship_id": 88, "target_player_id": 42}}
    assert fake_conn.calls[-1] == {"command": "ship.list", "data": {}}
    assert "transfer confirmed by server" in capsys.readouterr().out


def test_ship_transfer_rejects_schema_mismatched_receipt_ids_and_refreshes_roster(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    roster = {"status": "ok", "type": "ship.list.response", "data": {"ships": [
        {"ship_id": 88, "name": "Hauler", "active": False},
    ]}}
    fake_conn.queue("ship.list", roster)
    fake_conn.queue("ship.transfer", {"status": "ok", "type": "ship.transfer.response", "data": {
        "ship_id": True, "target_player_id": 42,
    }})
    fake_conn.queue("ship.list", roster)
    answers = iter(["1", "42", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.ship_transfer_flow(ctx_factory())

    assert [call["command"] for call in fake_conn.calls] == [
        "ship.list", "ship.transfer", "ship.list",
    ]
    output = capsys.readouterr().out
    assert "was not confirmed" in output
    assert "confirmed by server" not in output


def test_ship_transfer_does_not_use_typed_rows_from_refused_roster(
    ctx_factory, fake_conn, capsys
):
    """A refused roster cannot authorize an ownership mutation."""
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "refused", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 7, "name": "Hauler", "active": True}]},
        "error": {"message": "Ship roster is unavailable."},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 7, "active": True}},
    })

    ship_transfer_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.list", "ship.status"]
    output = capsys.readouterr().out
    assert "did not return an authoritative owned-ship roster" in output
    assert "ship.transfer" not in output


def test_transfer_cargo_selects_visible_target_and_refreshes_state(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 7, "ships": [
        {"id": 12, "name": "Active"}, {"id": 88, "name": "Hauler"},
    ]})
    ctx.player_info = {"player": {"ship_id": 12}}
    fake_conn.queue("ship.transfer_cargo", {"status": "ok", "type": "ship.cargo_transferred_v1", "data": {
        "source_ship_id": 12, "target_ship_id": 88, "commodity_id": 2,
        "quantity": 3, "source_remaining": 4, "target_quantity": 7,
    }})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 12}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {
        "sector_id": 7, "ships": [{"id": 12}, {"id": 88}], "ports": [], "planets": [],
    }})
    answers = iter(["1", "2", "3", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.transfer_cargo_flow(ctx)

    assert fake_conn.calls == [
        {"command": "ship.transfer_cargo", "data": {"commodity_id": 2, "quantity": 3, "target_ship_id": 88}},
        {"command": "ship.status", "data": {}},
        {"command": "move.describe_sector", "data": {}},
    ]
    assert ctx.last_sector_desc["id"] == 7
    assert "confirmed by server" in capsys.readouterr().out


def test_transfer_cargo_requires_confirmed_active_ship_before_mutation(
    ctx_factory, fake_conn, capsys
):
    """A stale client identity must not authorize a cargo mutation."""
    ctx = ctx_factory(sector={"id": 7, "ships": [{"id": 12}, {"id": 88}]})
    ctx.player_info = {"player": {"id": 3}}
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 12, "name": "Active"}},
    })

    client.transfer_cargo_flow(ctx)

    assert fake_conn.calls == [{"command": "ship.status", "data": {}}]
    output = capsys.readouterr().out
    assert "identity is not confirmed" in output
    assert "ship.transfer_cargo" not in output


def test_transfer_cargo_refusal_does_not_refresh_or_claim_success(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 7, "ships": [{"id": 12}, {"id": 88}]})
    ctx.player_info = {"player": {"ship_id": 12}}
    fake_conn.queue("ship.transfer_cargo", {"status": "error", "error": {"message": "Hold too small"}})
    answers = iter(["1", "2", "3", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.transfer_cargo_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.transfer_cargo"]
    assert "Hold too small" in capsys.readouterr().out


def test_transfer_cargo_connection_loss_reconciles_without_claiming_outcome(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 7, "ships": [{"id": 12}, {"id": 88}]})
    ctx.player_info = {"player": {"ship_id": 12}}
    original_rpc = fake_conn.rpc

    def disconnect_after_transfer(command, data):
        if command == "ship.transfer_cargo":
            raise ConnectionError("server disconnected")
        return original_rpc(command, data)

    monkeypatch.setattr(fake_conn, "rpc", disconnect_after_transfer)
    fake_conn.queue("ship.status", {"status": "ok", "type": "ship.status",
                                     "data": {"ship": {"id": 12}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "type": "sector.info",
                                              "data": {"sector_id": 7, "ships": [{"id": 12}, {"id": 88}],
                                                       "ports": [], "planets": []}})
    answers = iter(["1", "2", "3", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.transfer_cargo_flow(ctx)

    output = capsys.readouterr().out
    assert "outcome is uncertain after connection loss" in output
    assert "confirmed by server" not in output
    assert [call["command"] for call in fake_conn.calls] == [
        "ship.transfer_cargo", "ship.status", "move.describe_sector",
    ]
    assert ctx.last_sector_desc["id"] == 7


def test_transfer_cargo_does_not_apply_mismatched_ship_refresh(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A successful transfer must not make another ship's status local state."""
    ctx = ctx_factory(sector={"id": 7, "ships": [
        {"id": 12, "name": "Active"}, {"id": 88, "name": "Hauler"},
    ]})
    ctx.player_info = {"player": {"ship_id": 12}}
    ctx.hud = replace(ctx.hud, ship_id=12)
    fake_conn.queue("ship.transfer_cargo", {"status": "ok", "type": "ship.cargo_transferred_v1", "data": {
        "source_ship_id": 12, "target_ship_id": 88,
        "commodity_id": 2, "quantity": 3, "source_remaining": 4,
        "target_quantity": 7,
    }})
    fake_conn.queue("ship.status", {"status": "ok", "data": {
        "ship": {"id": 88, "hull": 1, "cargo": [{"commodity_id": 2, "quantity": 99}]},
    }})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {
        "sector_id": 7, "ships": [{"id": 12}, {"id": 88}], "ports": [], "planets": [],
    }})
    answers = iter(["1", "2", "3", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.transfer_cargo_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "ship.transfer_cargo", "ship.status", "move.describe_sector",
    ]
    assert ctx.hud.ship_id == 12
    assert "did not identify the confirmed active ship" in capsys.readouterr().out


def test_transfer_cargo_rejects_success_without_authoritative_receipt(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 7, "ships": [{"id": 12}, {"id": 88}]})
    ctx.player_info = {"player": {"ship_id": 12}}
    fake_conn.queue("ship.transfer_cargo", {"status": "ok", "data": {
        "source_ship_id": 12, "target_ship_id": 99, "commodity_id": 2,
        "quantity": 3,
    }})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 12}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {
        "sector_id": 7, "ships": [{"id": 12}, {"id": 88}], "ports": [], "planets": [],
    }})
    answers = iter(["1", "2", "3", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.transfer_cargo_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "ship.transfer_cargo", "ship.status", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "was not displayed" in output
    assert "confirmed by server" not in output


def test_transfer_cargo_rejects_untyped_success_even_when_fields_match(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """Only the server's typed cargo-transfer receipt may confirm lifecycle state."""
    ctx = ctx_factory(sector={"id": 7, "ships": [{"id": 12}, {"id": 88}]})
    ctx.player_info = {"player": {"ship_id": 12}}
    fake_conn.queue("ship.transfer_cargo", {"status": "ok", "data": {
        "source_ship_id": 12, "target_ship_id": 88, "commodity_id": 2,
        "quantity": 3, "source_remaining": 4, "target_quantity": 7,
    }})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 12}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {
        "sector_id": 7, "ships": [{"id": 12}, {"id": 88}], "ports": [], "planets": [],
    }})
    answers = iter(["1", "2", "3", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.transfer_cargo_flow(ctx)

    output = capsys.readouterr().out
    assert "was not displayed" in output
    assert "confirmed by server" not in output


def test_ship_upgrade_confirms_contract_and_refreshes_ship(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    fake_conn.queue("ship.upgrade", {"status": "ok", "data": {"upgrade_type": "shields"}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"shields": 12}}})
    answers = iter(["shields", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.ship_upgrade_flow(ctx_factory())

    assert fake_conn.calls == [
        {"command": "ship.upgrade", "data": {"upgrade_type": "shields"}},
        {"command": "ship.status", "data": {}},
    ]
    assert "confirmed by server" in capsys.readouterr().out


def test_ship_upgrade_refusal_does_not_refresh(ctx_factory, fake_conn, monkeypatch, capsys):
    fake_conn.queue("ship.upgrade", {"status": "error", "error": {"message": "Not docked"}})
    answers = iter(["shields", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.ship_upgrade_flow(ctx_factory())

    assert fake_conn.calls == [{"command": "ship.upgrade", "data": {"upgrade_type": "shields"}}]
    assert "Not docked" in capsys.readouterr().out


def test_shipyard_upgrade_refreshes_independently_and_preserves_refused_sector(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    old_sector = {"id": 7, "port": {"id": 3}}
    ctx = ctx_factory(sector=old_sector)
    ctx.state["towing_ship_id"] = 88
    fake_conn.queue("shipyard.list", {"status": "ok", "type": "shipyard.list_v1", "data": {"available": [
        {"type_id": 2, "name": "Scout", "net_cost": 500, "eligible": True, "reasons": []},
    ]}})
    fake_conn.queue("shipyard.upgrade", {"status": "ok", "type": "shipyard.upgraded_v1", "data": {
        "ship_id": 12, "new_type_id": 2, "new_ship_name": "Scout One", "credits_spent": 500,
    }})
    fake_conn.queue("player.my_info", {"status": "refused", "error": {"message": "Unavailable"}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"hull": 100}}})
    fake_conn.queue("move.describe_sector", {"status": "refused", "error": {"message": "Unavailable"}})
    answers = iter(["1", "Scout One", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.shipyard_upgrade_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "shipyard.list", "shipyard.upgrade", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert fake_conn.calls[1]["data"] == {"new_type_id": 2, "new_ship_name": "Scout One"}
    assert ctx.last_sector_desc == old_sector
    assert "towing_ship_id" not in ctx.state
    output = capsys.readouterr().out
    assert "displayed credits may be stale" in output
    assert "displayed sector may be stale" in output


def test_shipyard_upgrade_rejects_untyped_success_and_refreshes_authoritative_state(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 7})
    fake_conn.queue("shipyard.list", {"status": "ok", "type": "shipyard.list_v1", "data": {"available": [
        {"type_id": 2, "name": "Scout", "net_cost": 500, "eligible": True, "reasons": []},
    ]}})
    fake_conn.queue("shipyard.upgrade", {"status": "ok", "data": {
        "ship_id": 12, "new_type_id": 2, "new_ship_name": "Scout One", "credits_spent": 500,
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"credits": 500}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 12}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"sector_id": 7}})
    answers = iter(["1", "Scout One", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.shipyard_upgrade_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "shipyard.list", "shipyard.upgrade", "player.my_info", "ship.status", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "invalid shipyard receipt" in output
    assert "Successfully upgraded" not in output


def test_shipyard_upgrade_ambiguous_receipt_applies_typed_current_sector(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 7, "port": {"id": 3}})
    fake_conn.queue("shipyard.list", {"status": "ok", "type": "shipyard.list_v1", "data": {
        "available": [{"type_id": 2, "name": "Scout", "net_cost": 500, "eligible": True, "reasons": []}],
    }})
    fake_conn.queue("shipyard.upgrade", {"status": "ok", "data": {
        "ship_id": 12, "new_type_id": 2, "new_ship_name": "Scout One", "credits_spent": 500,
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "type": "player.info", "data": {"credits": 500}})
    fake_conn.queue("ship.status", {"status": "ok", "type": "ship.status", "data": {"ship": {"id": 12}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "type": "sector.info", "data": {"sector_id": 7, "name": "Alpha"}})
    answers = iter(["1", "Scout One", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.shipyard_upgrade_flow(ctx)

    assert ctx.last_sector_desc["id"] == 7
    assert ctx.last_sector_desc["name"] == "Alpha"
    assert "invalid shipyard receipt" in capsys.readouterr().out


def test_shipyard_upgrade_does_not_apply_wrong_typed_sector_after_success(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    original = {"id": 7, "port": {"id": 3}}
    ctx = ctx_factory(sector=original)
    fake_conn.queue("shipyard.list", {"status": "ok", "type": "shipyard.list_v1", "data": {
        "available": [{"type_id": 2, "name": "Scout", "net_cost": 500, "eligible": True, "reasons": []}],
    }})
    fake_conn.queue("shipyard.upgrade", {"status": "ok", "type": "shipyard.upgraded_v1", "data": {
        "ship_id": 12, "new_type_id": 2, "new_ship_name": "Scout One", "credits_spent": 500,
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "type": "player.info", "data": {"credits": 500}})
    fake_conn.queue("ship.status", {"status": "ok", "type": "ship.status", "data": {"ship": {"id": 12}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "type": "sector.info", "data": {"sector_id": 99}})
    answers = iter(["1", "Scout One", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.shipyard_upgrade_flow(ctx)

    assert ctx.last_sector_desc == original
    assert "displayed sector may be stale" in capsys.readouterr().out


def test_shipyard_upgrade_refusal_does_not_claim_success_or_change_state(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 7})
    fake_conn.queue("shipyard.list", {"status": "ok", "type": "shipyard.list_v1", "data": {"available": [
        {"type_id": 2, "name": "Scout", "net_cost": 500, "eligible": True, "reasons": []},
    ]}})
    fake_conn.queue("shipyard.upgrade", {"status": "refused", "error": {"message": "Insufficient credits"}})
    answers = iter(["1", "Scout One", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.shipyard_upgrade_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["shipyard.list", "shipyard.upgrade"]
    assert ctx.last_sector_desc == {"id": 7}
    output = capsys.readouterr().out
    assert "Insufficient credits" in output
    assert "Successfully upgraded" not in output


def test_shipyard_upgrade_rejects_untyped_catalogue_without_sending_exchange(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    fake_conn.queue("shipyard.list", {"status": "ok", "data": {"available": [
        {"type_id": 2, "name": "Scout", "net_cost": 500, "eligible": True, "reasons": []},
    ]}})

    client.shipyard_upgrade_flow(ctx_factory())

    assert fake_conn.calls == [{"command": "shipyard.list", "data": {}}]
    assert "unexpected response type" in capsys.readouterr().out


def test_shipyard_upgrade_rejects_catalogue_row_without_authoritative_type_id(
    ctx_factory, fake_conn, capsys
):
    fake_conn.queue("shipyard.list", {
        "status": "ok",
        "type": "shipyard.list_v1",
        "data": {"available": [{"name": "Scout", "eligible": True}]},
    })

    client.shipyard_upgrade_flow(ctx_factory())

    assert fake_conn.calls == [{"command": "shipyard.list", "data": {}}]
    assert "omitted its authoritative type ID" in capsys.readouterr().out


def test_shipyard_upgrade_requires_confirmation_and_explains_tax(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("shipyard.list", {"status": "ok", "type": "shipyard.list_v1", "data": {"available": [
        {"type_id": 2, "name": "Scout", "net_cost": 500, "eligible": True, "reasons": []},
    ]}})
    answers = iter(["1", "Scout One", ""])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.shipyard_upgrade_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["shipyard.list"]
    output = capsys.readouterr().out
    assert "Net before tax: 500" in output
    assert "excludes tax" in output
    assert "no exchange was sent" in output


def test_ship_card_displays_authoritative_hull_and_marks_older_server_field_missing():
    current = client.render_ship_card({"id": 12, "name": "Wayfarer", "hull": 73})
    older = client.render_ship_card({"id": 12, "name": "Wayfarer"})

    assert "Hull:   73/100" in current
    assert "Hull:   —/100" in older


def test_ship_card_does_not_render_invalid_hull_as_authoritative():
    for hull in (True, -1, 101):
        rendered = client.render_ship_card({"hull": hull})
        assert "Hull:   —/100" in rendered


def test_hardware_purchase_selects_catalogue_item_and_refreshes_ship(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("hardware.list", {"status": "ok", "data": {"hardware": [
        {"item_code": "SHIELDS", "name": "Shield generator", "price": 100},
        {"item_code": "TRANSWARP", "name": "Transwarp drive", "price": 500},
    ]}})
    fake_conn.queue("hardware.buy", {"status": "ok", "type": "hardware.purchase_v1", "data": {
        "code": "TRANSWARP", "quantity": 2, "credits_spent": 1000,
        "ship": {"has_transwarp": True},
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"credits": 900}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {
        "holds": 20, "fighters": 1, "shields": 2, "equipment": ["TRANSWARP"],
    }}})
    answers = iter(["2", "2"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.hardware_buy_flow(ctx)

    assert fake_conn.calls[0] == {"command": "hardware.list", "data": {}}
    assert fake_conn.calls[1] == {"command": "hardware.buy", "data": {
        "code": "TRANSWARP", "quantity": 2,
    }}
    assert [call["command"] for call in fake_conn.calls[2:]] == ["player.my_info", "ship.status"]
    assert "Successfully purchased 2 units of TRANSWARP" in capsys.readouterr().out


def test_hardware_purchase_rejects_untyped_success_and_refreshes_authoritative_state(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("hardware.list", {"status": "ok", "data": {"hardware": [
        {"item_code": "TRANSWARP", "name": "Transwarp drive", "price": 500},
    ]}})
    fake_conn.queue("hardware.buy", {"status": "ok", "data": {
        "quantity": 1, "item_code": "TRANSWARP",
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"credits": 900}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"has_transwarp": False}}})
    monkeypatch.setattr("builtins.input", lambda _prompt: "1" if "number" in _prompt else "1")

    client.hardware_buy_flow(ctx)

    output = capsys.readouterr().out
    assert "was not confirmed" in output
    assert "Successfully purchased" not in output
    assert [call["command"] for call in fake_conn.calls] == [
        "hardware.list", "hardware.buy", "player.my_info", "ship.status"
    ]


def test_hardware_purchase_rejects_item_not_in_server_catalogue(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("hardware.list", {"status": "ok", "data": {"hardware": [
        {"item_code": "SHIELDS", "name": "Shield generator", "price": 100},
    ]}})
    monkeypatch.setattr("builtins.input", lambda *_: "UNLISTED_DRIVE")

    client.hardware_buy_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["hardware.list"]
    assert "choose a hardware item from the server catalogue" in capsys.readouterr().out


def test_hardware_catalogue_refusal_is_presented_without_mutation(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("hardware.list", {
        "status": "refused", "error": {"message": "Dock at a supported hardware port."},
    })

    client.hardware_buy_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["hardware.list"]
    assert "Dock at a supported hardware port" in capsys.readouterr().out


def test_jettison_requires_confirmation_and_refreshes_authoritative_cargo(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("ship.info", {"status": "ok", "data": {"ship": {
        "cargo": [{"commodity": "ore", "quantity": 5}],
    }}})
    fake_conn.queue("ship.jettison", {"status": "ok", "data": {"remaining_cargo": [
        {"commodity": "ore", "quantity": 2},
    ]}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"credits": 100}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {
            "id": 7, "holds": 20, "cargo": [{"commodity": "ore", "qty": 2}],
    }}})
    answers = iter(["ORE", "3", "y"])
    prompts = []
    monkeypatch.setattr("builtins.input", lambda prompt: (prompts.append(prompt), next(answers))[1])

    client.jettison_cargo_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "ship.info", "ship.jettison", "player.my_info", "ship.status",
    ]
    assert fake_conn.calls[1]["data"] == {"commodity": "ORE", "quantity": 3}
    assert any("spend 1 turn" in prompt for prompt in prompts)
    assert "Successfully jettisoned" in capsys.readouterr().out
    assert ctx.hud.cargo_used == 2


def test_jettison_decline_does_not_send_turn_consuming_command(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("ship.info", {"status": "ok", "data": {"ship": {
        "cargo": [{"commodity": "ore", "quantity": 5}],
    }}})
    answers = iter(["ORE", "2", "n"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.jettison_cargo_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.info"]
    assert "Jettison cancelled" in capsys.readouterr().out


def test_jettison_over_reported_cargo_is_rejected_before_turn_spending(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("ship.info", {"status": "ok", "data": {"ship": {
        "cargo": [{"commodity": "ore", "quantity": 2}],
    }}})
    answers = iter(["ORE", "3"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.jettison_cargo_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.info"]
    assert "no jettison was sent" in capsys.readouterr().out


def test_claim_selected_ship_refreshes_independently_and_preserves_failed_sector(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    sector = {"id": 7, "ships": [{"id": 20, "name": "Drifter", "owner": "derelict"}]}
    ctx = ctx_factory(sector=sector)
    ctx.player_info = {"player": {"ship_id": 10}}
    ctx.state["towing_ship_id"] = 88
    fake_conn.queue("ship.claim", {"status": "ok", "data": {"ship": {"id": 20}}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"ship_id": 20}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"hull": 73}}})
    fake_conn.queue("move.describe_sector", {"status": "refused", "error": {"message": "Unavailable"}})
    answers = iter(["", "c", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.enter_ship_menu(ctx)

    assert fake_conn.calls[0] == {"command": "ship.claim", "data": {"ship_id": 20}}
    assert [call["command"] for call in fake_conn.calls[1:]] == ["player.my_info", "ship.status", "move.describe_sector"]
    assert client.get_my_ship_id(fake_conn, ctx) == 20
    assert ctx.last_sector_desc == sector
    assert "towing_ship_id" not in ctx.state
    assert "displayed sector may be stale" in capsys.readouterr().out


def test_invalid_claim_selection_never_defaults_to_first_ship(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory(sector={"id": 7, "ships": [{"id": 20, "owner": "derelict"}]})
    ctx.player_info = {"player": {"ship_id": 10}}
    monkeypatch.setattr("builtins.input", lambda *_: "99")

    client.enter_ship_menu(ctx)

    assert fake_conn.calls == []
    assert "no ship selected" in capsys.readouterr().out


def test_boardable_ship_requires_usable_id_and_known_owner_shape():
    assert not client.has_boardable_ship([{"name": "Unknown"}, {"id": 0}, {"id": 5, "owner": {"name": "Alice"}}])
    assert client.has_boardable_ship([{"id": 20, "owner": "derelict"}], my_ship_id=10)


def test_player_id_is_not_treated_as_current_ship_id(ctx_factory, fake_conn):
    ctx = ctx_factory()
    ctx.player_info = {"player": {"id": 20}, "ship": {"id": 42}}
    assert client.get_my_ship_id(fake_conn, ctx) == 42
    ctx.player_info = {"player": {"id": 20, "ship_id": True}}
    assert client.get_my_ship_id(fake_conn, ctx) is None


def test_owned_spare_ship_is_selectable_but_current_and_other_owned_ships_are_not():
    ships = [{"id": 10, "owner": "Captain"}, {"id": 20, "owner": "Captain"}]
    assert client.has_boardable_ship(ships, my_ship_id=10, my_name="Captain")
    assert not client.has_boardable_ship([ships[0]], my_ship_id=10, my_name="Captain")
    assert not client.has_boardable_ship([{"id": 30, "owner": "Other"}], my_ship_id=10, my_name="Captain")


def test_claim_refusal_does_not_refresh_or_replace_state(ctx_factory, fake_conn, monkeypatch, capsys):
    sector = {"id": 7, "ships": [{"id": 20, "owner": "derelict"}]}
    ctx = ctx_factory(sector=sector)
    ctx.player_info = {"player": {"ship_id": 10}}
    fake_conn.queue("ship.claim", {"status": "refused", "error": {"message": "Ship not claimable"}})
    answers = iter(["", "c", "y", "q"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.enter_ship_menu(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.claim"]
    assert ctx.player_info == {"player": {"ship_id": 10}}
    assert ctx.last_sector_desc == sector
    assert "Ship not claimable" in capsys.readouterr().out


def test_owned_spare_ship_can_be_selected_and_claimed(ctx_factory, fake_conn, monkeypatch):
    ctx = ctx_factory(sector={"id": 7, "ships": [
        {"id": 10, "name": "Current", "owner": "Captain"},
        {"id": 20, "name": "Spare", "owner": "Captain"},
    ]})
    ctx.player_info = {"player": {"ship_id": 10, "name": "Captain"}}
    fake_conn.queue("ship.claim", {"status": "ok", "data": {"ship": {"id": 20}}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"ship_id": 20}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 20}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 7, "ships": []}})
    answers = iter(["", "c", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.enter_ship_menu(ctx)

    assert fake_conn.calls[0] == {"command": "ship.claim", "data": {"ship_id": 20}}
    assert client.get_my_ship_id(fake_conn, ctx) == 20


def test_ship_transfer_selects_only_from_authoritative_owned_roster(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    roster = {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [
            {"ship_id": 12, "name": "Active", "active": True},
            {"ship_id": 88, "name": "Hauler", "active": False},
        ]},
    }
    fake_conn.queue("ship.list", roster)
    fake_conn.queue("ship.transfer", {
        "status": "ok", "type": "ship.transfer.response",
        "data": {"ship_id": 88, "from_player_id": 3, "target_player_id": 42},
    })
    fake_conn.queue("ship.list", roster)
    answers = iter(["2", "42", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.ship_transfer_flow(ctx)

    assert fake_conn.calls == [
        {"command": "ship.list", "data": {}},
        {"command": "ship.transfer", "data": {"ship_id": 88, "target_player_id": 42}},
        {"command": "player.my_info", "data": {}},
        {"command": "ship.status", "data": {}},
        {"command": "move.describe_sector", "data": {}},
        {"command": "ship.list", "data": {}},
    ]
    assert "Ship transfer confirmed by server" in capsys.readouterr().out


def test_ship_transfer_refusal_refreshes_authoritative_roster(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    roster = {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 88, "name": "Hauler", "active": False}]},
    }
    fake_conn.queue("ship.list", roster)
    fake_conn.queue("ship.transfer", {
        "status": "refused", "error": {"message": "Ship is not transferable"},
    })
    fake_conn.queue("ship.list", roster)
    answers = iter(["1", "42", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.ship_transfer_flow(ctx)

    assert fake_conn.calls == [
        {"command": "ship.list", "data": {}},
        {"command": "ship.transfer", "data": {"ship_id": 88, "target_player_id": 42}},
        {"command": "ship.list", "data": {}},
        {"command": "player.my_info", "data": {}},
        {"command": "ship.status", "data": {}},
        {"command": "move.describe_sector", "data": {}},
    ]
    output = capsys.readouterr().out
    assert "Ship transfer refused or failed" in output
    assert "Hauler" in output


def test_ship_transfer_success_does_not_apply_mismatched_sector_refresh(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """Ownership success must not replace the local sector with foreign data."""
    ctx = ctx_factory(sector={"id": 7, "name": "Confirmed sector"})
    ctx.state["player_id"] = 3
    roster = {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 88, "name": "Hauler", "active": False}]},
    }
    fake_conn.queue("ship.list", roster)
    fake_conn.queue("ship.transfer", {
        "status": "ok", "type": "ship.transfer.response",
        "data": {"ship_id": 88, "from_player_id": 3, "target_player_id": 42},
    })
    fake_conn.queue("player.my_info", {"status": "ok", "type": "player.info",
                                       "data": {"player": {"id": 3}}})
    fake_conn.queue("ship.status", {"status": "ok", "type": "ship.status",
                                     "data": {"ship": {"id": 1}}})
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "ship.status",
        "data": {"id": 99, "name": "Foreign sector"},
    })
    fake_conn.queue("ship.list", roster)
    answers = iter(["1", "42", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.ship_transfer_flow(ctx)

    assert ctx.last_sector_desc == {"id": 7, "name": "Confirmed sector"}
    assert "sector state could not be refreshed" in capsys.readouterr().out


def test_ship_transfer_refresh_rejects_foreign_player_and_active_ship_records(
    ctx_factory, fake_conn, capsys
):
    """A transfer receipt must not hydrate refresh data for another session."""
    ctx = ctx_factory()
    ctx.state["player_id"] = 3
    ctx.hud.ship_id = 1
    ctx.player_info = {"player": {"id": 3, "ship_id": 1}}
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 99, "ship_id": 99}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 99, "name": "Foreign ship"}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"id": 7, "name": "Confirmed sector"},
    })

    client._refresh_after_ship_transfer(ctx)

    assert ctx.player_info == {"player": {"id": 3, "ship_id": 1}}
    assert ctx.hud.ship_id == 1
    output = capsys.readouterr().out
    assert "logged-in player could not be confirmed" in output
    assert "confirmed active ship could not be refreshed" in output


def test_ship_transfer_does_not_send_for_invalid_roster_selection(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 88, "name": "Hauler", "active": False}]},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "2")

    client.ship_transfer_flow(ctx)

    assert fake_conn.calls == [{"command": "ship.list", "data": {}}]
    assert "valid owned-ship selection" in capsys.readouterr().out


def test_ship_transfer_connection_loss_reconciles_authoritative_roster(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    roster = {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 88, "name": "Hauler", "active": False}]},
    }
    fake_conn.queue("ship.list", roster)

    def transfer_disconnect(command, data):
        fake_conn.calls.append({"command": command, "data": data})
        raise ConnectionError("socket closed after request")

    original_rpc = fake_conn.rpc
    monkeypatch.setattr(
        fake_conn,
        "rpc",
        lambda command, data: transfer_disconnect(command, data)
        if command == "ship.transfer" else original_rpc(command, data),
    )
    answers = iter(["1", "42", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.ship_transfer_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "ship.list", "ship.transfer", "ship.list", "player.my_info",
        "ship.status", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "outcome is uncertain" in output
    assert "Hauler" in output
    assert ctx.state["last_rpc"] is None
