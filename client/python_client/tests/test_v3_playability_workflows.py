"""Player-facing workflows for server features that were previously hidden."""

import client


def test_dock_workflow_uses_server_confirmation_and_exposes_stardock_actions(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 216, "port": {"id": 207, "type": 9}})
    fake_conn.queue("dock.status", {
        "status": "ok", "type": "dock.status_v1", "data": {
            "port_id": 0, "sector_id": 216, "docked": False,
        },
    })
    fake_conn.queue("dock.status", {
        "status": "ok", "type": "dock.status_v1", "data": {
            "port_id": 207, "sector_id": 216, "docked": True,
        },
    })
    monkeypatch.setattr("builtins.input", lambda *_: "y")
    monkeypatch.setattr(client, "_refresh_ship_after_move", lambda _ctx: None)

    client.dock_status_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["dock.status", "dock.status"]
    assert fake_conn.calls[1]["data"] == {"action": "dock"}
    assert ctx.state["docked_port_id"] == 207
    assert "Ship docked at the local port" in capsys.readouterr().out


def test_fast_scan_displays_object_warp_rows_and_server_counts():
    rendered = client._render_scan_card({"status": "ok", "type": "sector.scan", "data": {
        "sector_id": 216, "name": "Canes",
        "adjacent_sectors": [{"to_sector": 292}, {"to_sector": 404}],
        "ships_present": [{"ship_id": 1}], "celestial_objects": [],
        "counts": {"ships": 1, "planets": 0, "mines": 0, "fighters": 0},
    }})
    assert "Adjacent: 292, 404" in rendered
    assert "ships=1, planets=0" in rendered


def test_harvest_flow_calls_server_and_refreshes_ship_and_planet(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 12})
    ctx.state.update(on_planet=True, current_planet_id=77)
    fake_conn.queue("planet.harvest", {
        "status": "ok", "type": "planet.harvest_v1", "data": {
            "planet_id": 77, "ship_id": 9, "ore_harvested": 3,
            "organics_harvested": 2, "equipment_harvested": 1,
            "holds_remaining": 4, "turns_charged": 0,
        },
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })
    fake_conn.queue("planet.info", {
        "status": "ok", "type": "planet.info", "data": {"planet_id": 77},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "y")

    client.planet_harvest_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.harvest", "ship.status", "planet.info",
    ]
    assert fake_conn.calls[0]["data"] == {"planet_id": 77}
    assert "Ore: 3; Organics: 2; Equipment: 1" in capsys.readouterr().out


def test_planetary_trade_flow_builds_idempotent_request_and_refreshes(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 12, "port": {"id": 88}})
    ctx.state.update(on_planet=True, current_planet_id=77)
    fake_conn.queue("planet.trade.port", {
        "status": "ok", "type": "planet.trade.port_receipt_v1", "data": {
            "planet_id": 77, "port_id": 88, "sector_id": 12,
            "lines": [{"commodity": "ORE", "quantity": 4, "credits": 20}],
        },
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {"player": {"id": 3}},
    })
    fake_conn.queue("planet.info", {
        "status": "ok", "type": "planet.info", "data": {"planet_id": 77},
    })
    answers = iter(["ORE", "4", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.planet_trade_port_flow(ctx)

    call = fake_conn.calls[0]
    assert call["command"] == "planet.trade.port"
    assert call["data"]["planet_id"] == 77
    assert call["data"]["port_id"] == 88
    assert call["data"]["items"] == [{"commodity": "ORE", "quantity": 4}]
    assert len(call["data"]["idempotency_key"]) == 36
    assert [item["command"] for item in fake_conn.calls] == [
        "planet.trade.port", "player.my_info", "planet.info",
    ]
    assert "Planetary Trade Agreement settled" in capsys.readouterr().out


def test_corporation_asset_transfer_flow_checks_roster_and_refreshes(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.state.update(in_corporation=True, player_id=42)
    fake_conn.queue("corp.status", {
        "status": "ok", "type": "corp.status.success", "data": {"corp_id": 5},
    })
    fake_conn.queue("corp.roster", {
        "status": "ok", "type": "corp.roster.success", "data": {
            "corp_id": 5, "roster": [{"player_id": 43, "name": "Second Captain"}],
        },
    })
    fake_conn.queue("corp.member.transfer", {
        "status": "ok", "type": "corp.member.asset_transferred_v1", "data": {
            "sender_player_id": 42, "recipient_player_id": 43,
            "asset": "credits", "quantity": 100,
            "sender_remaining": 900, "recipient_total": 600,
        },
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {"player": {"id": 42}},
    })
    answers = iter(["43", "1", "100", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.corp_member_asset_transfer_flow(ctx)

    transfer = next(call for call in fake_conn.calls if call["command"] == "corp.member.transfer")
    assert transfer["data"] == {"target_player_id": 43, "asset": "credits", "quantity": 100}
    assert "Transferred 100 credits to Second Captain." in capsys.readouterr().out


def test_surface_menu_exposes_harvest_and_planetary_trade(menus):
    actions = {
        option["key"]: option["action"].get("pycall")
        for option in menus["SURFACE"]["options"]
    }
    assert actions["h"] == "planet_harvest_flow"
    assert actions["p"] == "planet_trade_port_flow"


def test_corporation_menu_exposes_member_asset_transfer(menus):
    actions = {
        option["key"]: option["action"].get("pycall")
        for option in menus["CORPORATION_MAIN"]["options"]
    }
    assert actions["a"] == "corp_member_asset_transfer_flow"
