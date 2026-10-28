"""Regression coverage for the user-facing planetary management flows."""
import client


def _planet(**overrides):
    data = {
        "planet_id": 77,
        "name": "Test World",
        "class": "M",
        "sector_id": 10,
        "owner_type": "player",
        "owner_id": 3,
        "population": 200,
        "colonists_unassigned": 167,
        "colonists_ore": 20,
        "colonists_org": 5,
        "colonists_eq": 7,
        "colonists_weapons": 1,
        "fighters": 12,
        "citadel_level": 1,
        "goods": [{"commodity": "ORE", "quantity": 50, "max_capacity": 500}],
    }
    data.update(overrides)
    return {"status": "ok", "type": "planet.info", "data": data}


def test_planet_menus_expose_existing_management_commands(menus):
    surface = {item["key"]: item["action"] for item in menus["SURFACE"]["options"]}
    citadel = {item["key"]: item["action"] for item in menus["CITADEL"]["options"]}
    assert surface["i"] == {"pycall": "planet_overview_flow"}
    assert surface["w"] == {"pycall": "planet_workforce_flow"}
    assert surface["s"] == {"pycall": "planet_stock_transfer_flow"}
    assert surface["f"] == {"pycall": "planet_fighter_transfer_flow"}
    assert surface["r"] == {"pycall": "planet_rename_flow"}
    assert citadel["b"] == {"pycall": "citadel_build_flow"}
    assert citadel["d"] == {"pycall": "planet_treasury_deposit_flow"}
    assert citadel["w"] == {"pycall": "planet_treasury_withdraw_flow"}


def test_planet_overview_presents_authoritative_fields(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.info", _planet())

    client.planet_overview_flow(ctx)

    out = capsys.readouterr().out
    for expected in ("Test World", "Owner ID: 3", "Population: 200", "Weapons workers: 1",
                     "Planet fighters: 12", "Citadel level: 1", "ORE: 50 / 500"):
        assert expected in out


def test_workforce_allocation_sends_all_four_job_targets_and_refreshes(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.info", _planet())
    fake_conn.queue("planet.colonists.allocate", {
        "status": "ok", "data": {
            "colonists_unassigned": 152, "colonists_ore": 30,
            "colonists_org": 10, "colonists_eq": 5, "colonists_weapons": 3,
        },
    })
    fake_conn.queue("planet.info", _planet(colonists_unassigned=152,
                                             colonists_ore=30, colonists_org=10,
                                             colonists_eq=5, colonists_weapons=3))
    answers = iter(["30", "10", "5", "3"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.planet_workforce_flow(ctx)

    assert fake_conn.calls[1] == {
        "command": "planet.colonists.allocate",
        "data": {"planet_id": 77, "ore": 30, "organics": 10,
                 "equipment": 5, "weapons": 3},
    }
    assert fake_conn.calls[-1]["command"] == "planet.info"
    assert "Workforce allocation accepted" in capsys.readouterr().out


def test_workforce_rejects_population_overassignment_without_rpc(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.info", _planet())
    answers = iter(["100", "100", "1", "0"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.planet_workforce_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["planet.info"]
    assert "above the confirmed planet population" in capsys.readouterr().out


def test_workforce_displays_server_resource_or_ownership_refusal(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.info", _planet())
    fake_conn.queue("planet.colonists.allocate", {
        "status": "refused", "error": {"message": "You do not control this planet."},
    })
    answers = iter(["30", "10", "5", "3"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.planet_workforce_flow(ctx)

    assert "You do not control this planet." in capsys.readouterr().out
    assert len(fake_conn.calls) == 2


def test_planet_stock_deposit_uses_existing_payload_and_refreshes(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.info", _planet())
    fake_conn.queue("planet.deposit", {
        "status": "ok", "data": {"planet_quantity": 75, "ship_quantity": 25},
    })
    fake_conn.queue("planet.info", _planet())
    answers = iter(["d", "1", "25"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.planet_stock_transfer_flow(ctx)

    assert fake_conn.calls[1] == {
        "command": "planet.deposit",
        "data": {"planet_id": 77, "commodity": "ORE", "quantity": 25},
    }
    out = capsys.readouterr().out
    assert "25 ORE deposit accepted" in out
    assert "Planet stock: 75" in out


def test_treasury_withdraw_uses_credit_contract_and_reports_balance(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.info", _planet())
    fake_conn.queue("planet.withdraw", {
        "status": "ok", "data": {"planet_treasury_balance": 900, "player_credits": 1200},
    })
    fake_conn.queue("planet.info", _planet())
    monkeypatch.setattr("builtins.input", lambda _prompt: "100")

    client.planet_treasury_withdraw_flow(ctx)

    assert fake_conn.calls[1] == {
        "command": "planet.withdraw",
        "data": {"planet_id": 77, "commodity": "CREDITS", "quantity": 100},
    }
    out = capsys.readouterr().out
    assert "Treasury balance: 900" in out
    assert "Player credits: 1200" in out


def test_planet_fighter_transfer_uses_documented_direction_and_authoritative_result(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.info", _planet(fighters=12))
    fake_conn.queue("planet.fighters.transfer", {
        "status": "ok", "type": "planet.fighters.transferred_v1",
        "data": {"planet_id": 77, "direction": "ship_to_planet", "quantity": 5,
                 "planet_fighters": 17, "ship_fighters": 3},
    })
    fake_conn.queue("planet.info", _planet(fighters=17))
    answers = iter(["s", "5", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.planet_fighter_transfer_flow(ctx)

    assert fake_conn.calls[1] == {
        "command": "planet.fighters.transfer",
        "data": {"planet_id": 77, "direction": "ship_to_planet", "quantity": 5},
    }
    output = capsys.readouterr().out
    assert "Planet fighters: 17" in output
    assert "Ship fighters: 3" in output
    assert fake_conn.calls[-1]["command"] == "planet.info"


def test_planet_fighter_transfer_refusal_does_not_retry_or_refresh(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.info", _planet(fighters=12))
    fake_conn.queue("planet.fighters.transfer", {
        "status": "error", "error": {"message": "You do not control this planet."},
    })
    answers = iter(["p", "20", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.planet_fighter_transfer_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.info", "planet.fighters.transfer",
    ]
    assert "You do not control this planet." in capsys.readouterr().out


def test_citadel_build_uses_existing_command_and_reports_server_timing(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.info", _planet(citadel_level=0))
    fake_conn.queue("citadel.build", {
        "status": "ok", "type": "citadel.upgrade_started",
        "data": {"target_level": 1, "days_to_complete": 4, "completion_time": 123456},
    })
    fake_conn.queue("planet.info", _planet(citadel_level=0))
    monkeypatch.setattr("builtins.input", lambda _prompt: "y")

    client.citadel_build_flow(ctx)

    assert fake_conn.calls[1] == {"command": "citadel.build", "data": {"planet_id": 77}}
    out = capsys.readouterr().out
    assert "Server-reported duration: 4 day(s)" in out
    assert "exact requirement table" in out


def test_citadel_insufficient_resource_refusal_is_presented(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.info", _planet(citadel_level=0))
    fake_conn.queue("citadel.build", {
        "status": "refused", "error": {"message": "Insufficient equipment."},
    })
    monkeypatch.setattr("builtins.input", lambda _prompt: "yes")

    client.citadel_build_flow(ctx)

    assert "Citadel construction refused: Insufficient equipment." in capsys.readouterr().out
    assert len(fake_conn.calls) == 2


def test_planet_rename_validates_input_and_uses_server_payload(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.rename", {"status": "ok", "data": {"name": "New Terra"}})
    fake_conn.queue("planet.info", _planet(name="New Terra"))
    monkeypatch.setattr("builtins.input", lambda _prompt: "New Terra")

    client.planet_rename_flow(ctx)

    assert fake_conn.calls[0] == {
        "command": "planet.rename", "data": {"planet_id": 77, "new_name": "New Terra"},
    }
    assert "Planet renamed to New Terra" in capsys.readouterr().out


def test_planet_rename_server_error_is_displayed_without_refresh(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.rename", {
        "status": "error", "error": {"message": "Database temporarily unavailable."},
    })
    monkeypatch.setattr("builtins.input", lambda _prompt: "New Terra")

    client.planet_rename_flow(ctx)

    assert fake_conn.calls == [{
        "command": "planet.rename", "data": {"planet_id": 77, "new_name": "New Terra"},
    }]
    assert "Database temporarily unavailable" in capsys.readouterr().out
