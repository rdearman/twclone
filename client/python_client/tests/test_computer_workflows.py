"""Current-sector computer commands preserve server visibility boundaries."""
import client


def test_computer_menu_exposes_authoritative_sector_and_combat_reads(menus):
    options = {item["key"]: item for item in menus["COMPUTER"]["options"]}
    assert options["i"]["action"] == {"pycall": "sector_info_flow"}
    assert options["v"]["action"] == {"pycall": "detailed_sector_scan_flow"}
    assert options["m"]["action"]["rpc"] == {"command": "combat.status", "data": {}}
    assert options["e"]["action"] == {"pycall": "ferengi_traders_flow"}


def test_computer_menu_exposes_fine_workflows(menus):
    options = {item["key"]: item for item in menus["COMPUTER"]["options"]}
    assert options["a"]["action"] == {"pycall": "fine_list_flow"}
    assert options["y"]["action"] == {"pycall": "fine_pay_flow"}
    assert "fine_list_flow" in client.HANDLERS
    assert "fine_pay_flow" in client.HANDLERS


def test_computer_menu_exposes_trade_history(menus):
    options = {item["key"]: item for item in menus["COMPUTER"]["options"]}
    assert options["z"]["action"] == {"pycall": "trade_history_flow"}
    assert "trade_history_flow" in client.HANDLERS


def test_ship_rename_uses_active_ship_contract_and_refreshes_authoritative_state(
    menus, ctx_factory, fake_conn, monkeypatch, capsys
):
    options = {item["key"]: item for item in menus["COMPUTER"]["options"]}
    assert "unavailable" not in options["r"]["label"].lower()
    assert options["r"]["action"] == {"pycall": "rename_ship_flow"}
    fake_conn.queue("ship.rename", {"status": "ok", "type": "ship.renamed", "data": {"ship_id": 10, "name": "Aurora"}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 10, "name": "Aurora"}}})
    monkeypatch.setattr("builtins.input", lambda _prompt: "Aurora")

    client.rename_ship_flow(ctx_factory())

    assert fake_conn.calls == [
        {"command": "ship.rename", "data": {"name": "Aurora"}},
        {"command": "ship.status", "data": {}},
    ]
    assert "renamed to 'Aurora'" in capsys.readouterr().out


def test_ship_rename_refusal_does_not_refresh_or_claim_success(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    fake_conn.queue("ship.rename", {"status": "error", "error": {"message": "Name already in use"}})
    monkeypatch.setattr("builtins.input", lambda _prompt: "Aurora")

    client.rename_ship_flow(ctx_factory())

    assert fake_conn.calls == [{"command": "ship.rename", "data": {"name": "Aurora"}}]
    output = capsys.readouterr().out
    assert "refused or failed" in output
    assert "Aurora" not in output


def test_ship_rename_success_without_authoritative_name_does_not_claim_local_name(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    fake_conn.queue("ship.rename", {"status": "ok", "data": {"ship_id": 10}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"ship_id": 10}}})
    monkeypatch.setattr("builtins.input", lambda *_: "Aurora")

    client.rename_ship_flow(ctx_factory())

    assert fake_conn.calls == [
        {"command": "ship.rename", "data": {"name": "Aurora"}},
        {"command": "ship.status", "data": {}},
    ]
    output = capsys.readouterr().out
    assert "no authoritative ship name" in output
    assert "renamed to 'Aurora' (confirmed by server)" not in output


def test_computer_menu_exposes_supported_police_controls(menus):
    options = {item["key"]: item for item in menus["COMPUTER"]["options"]}
    assert options["x"]["action"] == {"pycall": "police_status_flow"}
    assert options["w"]["action"] == {"pycall": "police_bribe_flow"}
    assert options["u"]["action"] == {"pycall": "police_surrender_flow"}
    assert all(name in client.HANDLERS for name in (
        "police_status_flow", "police_bribe_flow", "police_surrender_flow"
    ))


def test_police_status_displays_server_report_without_calculating_state(
    ctx_factory, fake_conn, capsys
):
    fake_conn.queue("police.status", {"status": "ok", "data": {
        "cluster_id": 4, "suspicion": 2, "wanted_level": 1, "banned": False,
    }})
    client.police_status_flow(ctx_factory())
    assert fake_conn.calls == [{"command": "police.status", "data": {}}]
    assert "Suspicion: 2" in capsys.readouterr().out


def test_police_bribe_requires_positive_amount_and_explicit_confirmation(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    answers = iter(["0", "250", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))
    fake_conn.queue("police.bribe", {"status": "ok", "data": {
        "message": "Bribe accepted.", "suspicion_after": 0, "wanted_level_after": 0,
    }})
    client.police_bribe_flow(ctx_factory())
    assert "positive" in capsys.readouterr().out
    assert fake_conn.calls == []

    client.police_bribe_flow(ctx_factory())
    assert fake_conn.calls == [{"command": "police.bribe", "data": {"amount": 250}}]
    assert "Bribe accepted." in capsys.readouterr().out


def test_police_surrender_requires_confirmation_and_shows_refusal(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    answers = iter(["n", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))
    fake_conn.queue("police.surrender", {"status": "error", "error": {
        "message": "You have nothing to surrender for",
    }})
    client.police_surrender_flow(ctx_factory())
    assert fake_conn.calls == []
    assert "cancelled" in capsys.readouterr().out

    client.police_surrender_flow(ctx_factory())
    assert fake_conn.calls == [{"command": "police.surrender", "data": {}}]
    assert "nothing to surrender" in capsys.readouterr().out


def test_police_surrender_displays_only_server_confirmed_outcome(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    fake_conn.queue("police.surrender", {"status": "ok", "data": {
        "message": "You have surrendered to the Federation.",
        "suspicion_after": 0, "wanted_level_after": 0,
    }})
    monkeypatch.setattr("builtins.input", lambda _prompt: "y")
    client.police_surrender_flow(ctx_factory())
    assert fake_conn.calls == [{"command": "police.surrender", "data": {}}]
    output = capsys.readouterr().out
    assert "surrendered to the Federation" in output
    assert "wanted level: 0" in output


def test_attack_in_fedspace_is_blocked_before_rpc(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 10, "ships": [{"id": 20, "name": "Target"}]})
    ctx.player_info = {"player": {"ship_id": 10}}

    client.combat_attack_flow(ctx)

    assert fake_conn.calls == []
    assert "Captain Z protects FedSpace" in capsys.readouterr().out


def test_attack_requires_target_from_current_server_list(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory(sector={"id": 22, "ships": [{"id": 20, "name": "Target"}]})
    ctx.player_info = {"player": {"ship_id": 10}}
    monkeypatch.setattr("builtins.input", lambda *_: "21")

    client.combat_attack_flow(ctx)

    assert fake_conn.calls == []
    assert "not listed in the current sector" in capsys.readouterr().out


def test_attack_reports_server_refusal_without_refresh_or_retry(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 22, "ships": [{"id": 20, "name": "Target"}]})
    ctx.player_info = {"player": {"ship_id": 10}}
    fake_conn.queue("combat.attack", {
        "status": "refused", "error": {"message": "Target is protected."},
    })
    answers = iter(["20", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.combat_attack_flow(ctx)

    assert fake_conn.calls == [{"command": "combat.attack", "data": {"target_ship_id": 20}}]
    assert "Target is protected" in capsys.readouterr().out


def test_attack_prints_server_report_then_refreshes_ship_and_sector(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 22, "ships": [{"id": 20, "name": "Target"}]})
    ctx.player_info = {"player": {"ship_id": 10}}
    fake_conn.queue("combat.attack", {
        "status": "ok", "type": "combat.attack", "data": {"damage": 12, "target_ship_id": 20},
    })
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 10, "fighters": 4}}})
    fake_conn.queue("sector.info", {"status": "ok", "data": {
        "sector_id": 22, "ships_present": [{"id": 20}], "ports": [], "planets": [],
    }})
    answers = iter(["20", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.combat_attack_flow(ctx)

    assert fake_conn.calls[0] == {"command": "combat.attack", "data": {"target_ship_id": 20}}
    assert [call["command"] for call in fake_conn.calls[1:]] == ["ship.status", "sector.info"]
    output = capsys.readouterr().out
    assert "Server Combat Report" in output and '"damage": 12' in output
