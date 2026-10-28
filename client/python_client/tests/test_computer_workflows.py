"""Current-sector computer commands preserve server visibility boundaries."""
import client


def test_computer_menu_exposes_authoritative_sector_and_combat_reads(menus):
    options = {item["key"]: item for item in menus["COMPUTER"]["options"]}
    assert options["i"]["action"] == {"pycall": "sector_info_flow"}
    assert options["v"]["action"] == {"pycall": "detailed_sector_scan_flow"}
    assert options["m"]["action"]["rpc"] == {"command": "combat.status", "data": {}}
    assert "e" not in options


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
