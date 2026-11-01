"""Regression coverage for the user-facing planetary management flows."""
import client
import pytest
from uuid import UUID


def test_genesis_preserves_sector_when_refresh_is_refused(ctx_factory, fake_conn, monkeypatch, capsys):
    sector = {"id": 17, "name": "Known sector"}
    ctx = ctx_factory(sector=sector)
    fake_conn.queue("planet.genesis_create", {"status": "ok", "data": {"planet_id": 77, "name": "Terra"}})
    fake_conn.queue("ship.status", {"status": "refused", "error": {"message": "Unavailable"}})
    fake_conn.queue("move.describe_sector", {"status": "refused", "error": {"message": "Unavailable"}})
    answers = iter(["Terra", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.genesis_torpedo_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.genesis_create", "ship.status", "move.describe_sector",
    ]
    payload = fake_conn.calls[0]["data"]
    assert {k: payload[k] for k in ("sector_id", "name", "owner_entity_type")} == {
        "sector_id": 17, "name": "Terra", "owner_entity_type": "player",
    }
    assert UUID(payload["idempotency_key"]).version == 4
    assert ctx.last_sector_desc == sector
    output = capsys.readouterr().out
    assert "displayed equipment may be stale" in output
    assert "displayed sector may be stale" in output


def test_genesis_refusal_preserves_sector_without_claiming_success(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory(sector={"id": 17})
    fake_conn.queue("planet.genesis_create", {"status": "refused", "error": {"message": "No torpedoes"}})
    answers = iter(["Terra", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.genesis_torpedo_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["planet.genesis_create"]
    assert ctx.last_sector_desc == {"id": 17}
    output = capsys.readouterr().out
    assert "No torpedoes" in output
    assert "successful" not in output


def test_landing_success_uses_listed_id_and_refreshes_authoritative_state(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "ok", "type": "planet.land.success", "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 41}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 100, "onplanet": 77}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"id": 17, "name": "Confirmed sector", "planets": [{"id": 77, "name": "Terra"}]},
    })
    fake_conn.queue("planet.info", _planet(planet_id=77, sector_id=17))

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector", "planet.info",
    ]
    assert fake_conn.calls[0]["data"] == {"planet_id": 77}
    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack[-1] == "SURFACE"
    assert "Landed on planet 77." in capsys.readouterr().out


def test_landing_success_applies_canonical_hud_refresh_before_surface(
    ctx_factory, fake_conn, capsys
):
    """The documented player/ship snapshots must reach the landing HUD."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "ok", "type": "planet.land.success", "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 3, "turns_remaining": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 9, "hull": 100, "onplanet": 77}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"id": 17, "name": "Confirmed sector", "planets": [{"id": 77, "name": "Terra"}]},
    })
    fake_conn.queue("planet.info", _planet(planet_id=77, sector_id=17))

    client.land_flow(ctx)

    assert ctx.hud.turns_remaining == 39
    assert ctx.hud.ship_id == 9
    assert ctx.hud.hull == 100
    assert ctx.state["on_planet"] is True
    assert ctx.menu_stack[-1] == "SURFACE"
    assert "Landed on planet 77." in capsys.readouterr().out


def test_planet_info_does_not_display_a_different_authoritative_planet(
    ctx_factory, fake_conn, capsys
):
    """A successful planet.info reply must remain correlated to its request."""
    ctx = ctx_factory(sector={"id": 17})
    fake_conn.queue("planet.info", _planet(planet_id=88, sector_id=17))

    client._refresh_planet_overview(ctx, 77)

    assert fake_conn.calls[0] == {"command": "planet.info", "data": {"planet_id": 77}}
    assert "planet.info identified planet 88" in capsys.readouterr().out


def test_landing_success_with_mismatched_player_refresh_stays_non_actionable(
    ctx_factory, fake_conn, capsys
):
    """A landing success cannot open a surface on a refresh for another player."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.player_info = {"player": {"id": 3, "turns": 40}}
    fake_conn.queue("planet.land", {
        "status": "ok", "type": "planet.land.success", "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 99, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 100}},
    })

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status",
    ]
    assert ctx.player_info == {"player": {"id": 3, "turns": 40}}
    assert ctx.state.get("on_planet") is False
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack[-1] != "SURFACE"
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out


def test_landing_wrong_response_type_does_not_claim_landing_and_refreshes(
    ctx_factory, fake_conn, capsys
):
    """A success-shaped reply for another command is not a landing confirmation."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "ok", "type": "planet.info", "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 40}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 9, "onplanet": 77}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    })

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert ctx.state.get("on_planet") is not True
    assert "Landing response had unexpected type planet.info" in capsys.readouterr().out


def test_landing_refusal_refreshes_without_claiming_landing(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.state["current_planet_id"] = 12
    fake_conn.queue("planet.land", {
        "status": "refused", "error": {"message": "Planetary defenses refused landing."},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 40}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 85}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    })

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert ctx.state["current_planet_id"] == 12
    assert ctx.state.get("on_planet") is not True
    output = capsys.readouterr().out
    assert "Landing failed: Planetary defenses refused landing." in output


def test_landing_connection_loss_does_not_claim_surface_or_retry(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """An ambiguous landing reply stays pending for reconnect reconciliation."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.state["current_planet_id"] = 12
    ctx.state["on_planet"] = False

    def lost_connection(_command, _data):
        raise ConnectionError("socket closed while landing")

    monkeypatch.setattr(fake_conn, "rpc", lost_connection)

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is False
    assert ctx.state["current_planet_id"] == 12
    assert ctx.menu_stack[-1] != "SURFACE"
    assert "Landing outcome is uncertain after connection loss" in capsys.readouterr().out


def test_landing_defense_refusal_renders_authoritative_combat_and_refreshes(
    ctx_factory, fake_conn, capsys
):
    """A defense refusal reports server combat without opening the planet surface."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 12
    fake_conn.queue("planet.land", {
        "status": "error",
        "error": {
            "message": "Planetary defenses engaged your ship; landing refused.",
            "data": {
                "planet_id": 77,
                "landing_allowed": False,
                "ship_remains_in_sector": True,
                "combat": [{
                    "defense": "atmosphere_quasar",
                    "damage": 25,
                    "shields_lost": 10,
                    "hull_lost": 15,
                    "shields_remaining": 0,
                    "hull_remaining": 85,
                    "destroyed": False,
                }],
            },
        },
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 85}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    })

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 12
    output = capsys.readouterr().out
    assert "Atmospheric Quasar: 25 damage" in output
    assert "shields lost 10, hull lost 15" in output
    assert "Landing was refused; no planetary access was granted." in output


def test_landing_refusal_reads_authoritative_combat_from_response_data(
    ctx_factory, fake_conn, capsys
):
    """A refusal report remains server-authored when data is envelope-level."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "refused",
        "error": {"message": "Planetary defenses refused landing."},
        "data": {
            "planet_id": 77,
            "landing_allowed": False,
            "ship_remains_in_sector": True,
            "combat": [{
                "defense": "planetary_fighters",
                "result": "defended",
                "attacker_fighters_lost": 4,
                "attacker_fighters_remaining": 6,
                "planet_fighters_lost": 2,
                "planet_fighters_remaining": 18,
                "citadel_shields_absorbed": 3,
                "citadel_shields_remaining": 7,
                "destroyed": False,
            }],
        },
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 85}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    })

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert ctx.state.get("on_planet") is not True
    output = capsys.readouterr().out
    assert "Planetary fighters defended" in output
    assert "ship lost 4 fighters (6 remain)" in output
    assert "Landing was refused; no planetary access was granted." in output


def test_landing_malformed_reply_refreshes_without_crashing(
    ctx_factory, fake_conn, capsys
):
    """An unusable server reply must not become a locally claimed landing."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", [])
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 40}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 100}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    })

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert ctx.state.get("on_planet") is not True
    assert "malformed landing response" in capsys.readouterr().out


def test_landing_partial_success_does_not_infer_planet_context(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"message": "Landed"}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"turns": 39}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"hull": 100}}})
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    })

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert "current_planet_id" not in ctx.state
    assert "Landing succeeded, but the server did not return the requested planet ID." in capsys.readouterr().out


def test_landing_mismatched_success_id_does_not_open_wrong_surface(
    ctx_factory, fake_conn, capsys
):
    """A success envelope for another planet is not a landing confirmation."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "ok", "type": "planet.land.success", "data": {"planet_id": 88},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 100}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    })

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert ctx.state.get("on_planet") is not True
    assert "current_planet_id" not in ctx.state
    assert "did not return the requested planet ID" in capsys.readouterr().out


def test_landing_index_rejects_planet_record_with_unusable_authoritative_id(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """An indexed display row must not bypass the authoritative-ID guard."""
    ctx = ctx_factory(sector={
        "id": 17,
        "planets": [
            {"id": 77, "name": "Terra"},
            {"name": "Unidentified world"},
        ],
    })
    monkeypatch.setattr("builtins.input", lambda _prompt: "2")

    client.land_flow(ctx)

    assert fake_conn.calls == []
    assert "Could not determine a listed planet ID" in capsys.readouterr().out


def test_launch_refreshes_authoritative_state_and_displays_entry_hazard(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "name": "Planet sector"})
    ctx.state["on_planet"] = True
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.launch", {
        "status": "ok",
        "data": {"sector_id": 17, "hazards": [
            {"hazard_type": "radiation", "hull_lost": 12},
        ]},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 38}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 9, "hull": 88}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"sector_id": 17, "name": "Planet sector"},
    })

    client.launch_flow(ctx)

    assert ctx.state["on_planet"] is False
    assert ctx.last_sector_desc["id"] == 17
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.launch", "player.my_info", "ship.status", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "radiation" in output and "hull lost: 12" in output
    assert "Launched from planet." in output


def test_failed_launch_reconciles_without_clearing_landed_context(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "name": "Planet sector"})
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.launch", {
        "status": "error", "error": {"message": "Launch transaction rolled back."},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"hull": 100}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"sector_id": 17, "name": "Planet sector"},
    })

    client.launch_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack[-1] == "SURFACE"
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.launch", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert "Launch transaction rolled back." in capsys.readouterr().out


def test_ambiguous_successful_launch_does_not_claim_departure(
    ctx_factory, fake_conn, capsys
):
    """A successful envelope without the authoritative sector is not enough."""
    ctx = ctx_factory(sector={"id": 17, "name": "Planet sector"})
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.launch", {
        "status": "ok", "type": "planet.launch.success",
        "data": {"message": "Launch processed"},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"turns": 38}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"hull": 100}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"sector_id": 17, "name": "Planet sector"},
    })

    client.launch_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack[-1] == "SURFACE"
    assert "did not confirm the launch destination sector" in capsys.readouterr().out


def test_landing_without_authoritative_planet_id_sends_no_request(
    ctx_factory, fake_conn, capsys
):
    """A display-only planet row must never be promoted to a landing target."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"name": "Unidentified world"}]})

    client.land_flow(ctx)

    assert fake_conn.calls == []
    assert ctx.state.get("on_planet") is not True
    assert "No listed planet has a usable server ID." in capsys.readouterr().out


@pytest.mark.parametrize("malformed_id", [True, 77.0, 77.5])
def test_landing_rejects_non_integer_authoritative_planet_id(
    ctx_factory, fake_conn, capsys, malformed_id
):
    """Boolean/fractional metadata must not become a landing target by coercion."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": malformed_id, "name": "Untrusted world"}]})

    client.land_flow(ctx)

    assert fake_conn.calls == []
    assert ctx.state.get("on_planet") is not True
    assert "No listed planet has a usable server ID." in capsys.readouterr().out


@pytest.mark.parametrize("stale_id", [True, 77.0, 77.5, "77"])
def test_planet_follow_up_rejects_non_integer_cached_id(
    ctx_factory, fake_conn, capsys, stale_id
):
    """Stale cached metadata must not become a planet command target."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.state["current_planet_id"] = stale_id

    client.planet_workforce_flow(ctx)

    assert fake_conn.calls == []
    assert "Land on a planet before assigning its workforce." in capsys.readouterr().out


def test_planet_follow_up_uses_valid_planet_id_when_legacy_id_is_malformed(
    ctx_factory, fake_conn, capsys
):
    """A bad display alias must not hide the server's valid planet_id."""
    ctx = ctx_factory(sector={
        "id": 17,
        "planets": [{"id": "77", "planet_id": 77, "name": "Terra"}],
    })
    fake_conn.queue("planet.info", _planet(planet_id=77))

    client.planet_overview_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["planet.info"]
    assert fake_conn.calls[0]["data"] == {"planet_id": 77}
    assert "Test World" in capsys.readouterr().out


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


def test_planet_harvest_is_visible_but_fail_closed_until_server_policy_exists(
    menus, ctx_factory, fake_conn, capsys
):
    surface = {item["key"]: item for item in menus["SURFACE"]["options"]}
    assert surface["h"]["action"] == {"pycall": "planet_harvest_unavailable_flow"}

    client.planet_harvest_unavailable_flow(ctx_factory())

    assert fake_conn.calls == []
    assert "authoritative server handler" in capsys.readouterr().out


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


def test_planet_stock_transfer_rejects_contradictory_authoritative_planet_id(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A transfer reply for another planet must not be presented as success."""
    ctx = ctx_factory()
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.info", _planet())
    fake_conn.queue("planet.deposit", {
        "status": "ok",
        "data": {"planet_id": 88, "planet_quantity": 75, "ship_quantity": 25},
    })
    fake_conn.queue("planet.info", _planet())
    # The response above is intentionally contradictory and must only trigger
    # a read-back, not a success claim.
    answers = iter(["d", "1", "25"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))
    client.planet_stock_transfer_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.info", "planet.deposit", "planet.info",
    ]
    output = capsys.readouterr().out
    assert "was not confirmed" in output
    assert "deposit accepted" not in output


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


def test_citadel_build_rejects_malformed_authoritative_level(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A schema-mismatched level must not authorize a build request."""
    ctx = ctx_factory()
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.info", _planet(citadel_level="0"))
    monkeypatch.setattr("builtins.input", lambda _prompt: "y")

    client.citadel_build_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["planet.info"]
    assert "valid authoritative citadel level" in capsys.readouterr().out


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


def test_hostile_planet_landing_reports_server_damage_and_refreshes_ship(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Citadel"}]})
    fake_conn.queue("planet.land", {
        "status": "error",
        "error": {"message": "Planetary defenses engaged your ship; landing refused.", "data": {
            "planet_id": 77,
            "landing_allowed": False,
            "ship_remains_in_sector": True,
            "combat": [
                {"defense": "atmosphere_quasar", "damage": 80, "shields_lost": 50,
                 "hull_lost": 30, "shields_remaining": 0, "hull_remaining": 70,
                 "destroyed": False},
                {"result": "repelled", "attacker_fighters_lost": 10,
                 "attacker_fighters_remaining": 5, "planet_fighters_lost": 5,
                 "planet_fighters_remaining": 15, "citadel_shields_absorbed": 20,
                 "citadel_shields_remaining": 0},
            ],
        }},
    })
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 1, "sector": 42}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "hull": 70}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 42, "planets": [{"id": 77, "name": "Citadel"}]}})

    client.land_flow(ctx)

    out = capsys.readouterr().out
    assert "shields lost 50" in out and "hull lost 30" in out
    assert "Planetary fighters repelled" in out
    assert "ship lost 10 fighters (5 remain)" in out
    assert "Landing was refused" in out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert ctx.state.get("on_planet") is not True


def test_planet_landing_without_combat_does_not_claim_damage(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    fake_conn.queue("planet.land", {
        "status": "error", "error": {"message": "Permission denied.", "data": None},
    })
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 1, "sector": 42}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 42, "planets": [{"id": 77, "name": "Private"}]}})

    client.land_flow(ctx)

    assert "Landing failed: Permission denied." in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert ctx.state["last_rpc"]["status"] == "error"


def test_planet_landing_rejects_unlisted_typed_id_without_rpc(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 42, "planets": [
        {"id": 77, "name": "Private"}, {"id": 78, "name": "Other"},
    ]})
    monkeypatch.setattr("builtins.input", lambda _prompt: "999")

    client.land_flow(ctx)

    assert fake_conn.calls == []
    assert "listed planet ID" in capsys.readouterr().out


def test_planet_landing_rejects_malformed_sector_record_without_rpc(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77}, "untrusted-record"]})

    client.land_flow(ctx)

    assert fake_conn.calls == []
    assert "invalid planet record" in capsys.readouterr().out


def test_planet_landing_does_not_invent_id_for_listed_planet(
    ctx_factory, fake_conn, capsys
):
    """A visible planet without a server ID cannot become a landing target."""
    ctx = ctx_factory(sector={"id": 42, "planets": [{"name": "Unidentified world"}]})

    client.land_flow(ctx)

    assert fake_conn.calls == []
    assert "usable server ID" in capsys.readouterr().out


def test_planet_landing_rejects_malformed_sector_envelope_without_rpc(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory()
    ctx.last_sector_desc = []

    client.land_flow(ctx)

    assert fake_conn.calls == []
    assert "current-sector information is malformed" in capsys.readouterr().out


def test_planet_landing_uses_typed_id_when_primary_id_is_null(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 42, "planets": [
        {"id": None, "planet_id": 77, "name": "Private"},
        {"id": 78, "name": "Other"},
    ]})
    fake_conn.queue("planet.land", {"status": "refused", "error": {"message": "Permission denied."}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"sector": 42}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 42, "planets": []}})
    monkeypatch.setattr("builtins.input", lambda _prompt: "1")

    client.land_flow(ctx)

    assert fake_conn.calls[0] == {"command": "planet.land", "data": {"planet_id": 77}}
    assert "Permission denied." in capsys.readouterr().out


def test_planet_landing_enters_surface_only_for_authoritative_id(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    # The server's sole successful landing contract is the typed
    # ``planet.land.success`` envelope.  An untyped success-shaped payload is
    # intentionally not a compatible response variant.
    fake_conn.queue("planet.land", {
        "status": "ok", "type": "planet.land.success",
        "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 1, "sector": 42, "turns": 98}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "onplanet": 77}}})
    fake_conn.queue("planet.info", _planet())

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector", "planet.info",
    ]
    assert "Landed on planet 77." in capsys.readouterr().out


def test_planet_landing_does_not_open_surface_for_ship_on_different_planet(
    ctx_factory, fake_conn, capsys
):
    """A post-landing ship refresh must confirm the requested planet identity."""
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    fake_conn.queue("planet.land", {
        "status": "ok", "type": "planet.land.success", "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 1, "sector": 42, "turns": 98}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 9, "onplanet": 88}},
    })

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status",
    ]
    assert ctx.state["on_planet"] is False
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack[-1] != "SURFACE"
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out


def test_planet_landing_warns_when_post_landing_refresh_is_refused(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {"status": "refused", "error": {"message": "Session is stale."}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9}}})
    fake_conn.queue("planet.info", _planet())

    client.land_flow(ctx)

    output = capsys.readouterr().out
    assert ctx.state["on_planet"] is False
    assert ctx.state["current_planet_id"] == 77
    assert "post-landing HUD refresh was incomplete" in output
    assert "displayed state may be stale" in output


def test_incomplete_relanding_does_not_leave_stale_surface_menu_reachable(
    ctx_factory, fake_conn, capsys
):
    """A replacement landing must not preserve an old actionable surface frame."""
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    ctx.state.update({"on_planet": True, "current_planet_id": 66})
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {"status": "refused", "error": {"message": "Session is stale."}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "onplanet": 88}}})

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is False
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack == ["MAIN"]
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out


def test_successful_relanding_replaces_stale_surface_frame(
    ctx_factory, fake_conn, capsys
):
    """A confirmed replacement landing exposes exactly one surface frame."""
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    ctx.state.update({"on_planet": True, "current_planet_id": 66})
    ctx.menu_stack.extend(["SURFACE", "SURFACE"])
    fake_conn.queue("planet.land", {
        "status": "ok", "type": "planet.land.success", "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9, "onplanet": 77}},
    })
    fake_conn.queue("planet.info", _planet(planet_id=77, sector_id=42))

    client.land_flow(ctx)

    assert ctx.menu_stack == ["MAIN", "SURFACE"]
    assert ctx.state["current_planet_id"] == 77
    assert "Landed on planet 77." in capsys.readouterr().out


def test_planet_landing_does_not_open_surface_after_refresh_disconnect(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A lost post-landing refresh reply leaves the surface non-actionable."""
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 42}})

    original_rpc = fake_conn.rpc

    def disconnect_on_player_refresh(command, data):
        if command == "player.my_info":
            fake_conn.calls.append({"command": command, "data": data})
            raise ConnectionError("server disconnected during refresh")
        return original_rpc(command, data)

    monkeypatch.setattr(fake_conn, "rpc", disconnect_on_player_refresh)

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status",
    ]
    assert ctx.state["on_planet"] is False
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack[-1] != "SURFACE"
    output = capsys.readouterr().out
    assert "post-landing HUD refresh was incomplete" in output
    assert "Reconnect before relying on turns" in output


def test_planet_landing_disconnect_does_not_retry_or_claim_surface(
    ctx_factory, fake_conn, capsys
):
    """A lost landing reply is ambiguous and must remain non-actionable."""
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})

    def disconnect_on_landing(command, data):
        fake_conn.calls.append({"command": command, "data": data})
        raise ConnectionError("server disconnected during landing")

    fake_conn.rpc = disconnect_on_landing

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["planet.land"]
    assert ctx.state.get("on_planet") is not True
    assert "current_planet_id" not in ctx.state
    assert ctx.menu_stack[-1] != "SURFACE"
    output = capsys.readouterr().out
    assert "Landing outcome is uncertain after connection loss" in output
    assert "Reconnect before attempting another landing" in output


def test_planet_overview_disconnect_does_not_crash_or_keep_surface_menu(
    ctx_factory, fake_conn, capsys
):
    """A lost planet read must not expose stale surface actions."""
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    ctx.state.update({"on_planet": True, "current_planet_id": 77})
    ctx.menu_stack.append("SURFACE")

    def disconnect(_command, _data):
        raise ConnectionError("server disconnected during planet read")

    fake_conn.rpc = disconnect

    client.planet_overview_flow(ctx)

    output = capsys.readouterr().out
    assert "Planet information outcome is uncertain after connection loss" in output
    assert "Reconnect before relying on planetary records" in output
    assert ctx.menu_stack[-1] != "SURFACE"
    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77


def test_planet_landing_warns_when_successful_hud_refresh_payload_is_malformed(
    ctx_factory, fake_conn, capsys
):
    """An OK envelope without typed HUD records is not an authoritative refresh."""
    original_player = {"player": {"id": 1, "sector": 42, "turns": 99}}
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    ctx.player_info = original_player
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"message": "not a player record"}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"message": "not a ship record"}})
    fake_conn.queue("planet.info", _planet())

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is False
    assert ctx.state["current_planet_id"] == 77
    assert ctx.player_info == original_player
    output = capsys.readouterr().out
    assert "post-landing HUD refresh was incomplete" in output
    assert "displayed state may be stale" in output


def test_planet_landing_does_not_claim_surface_without_authoritative_id(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"message": "Landed"}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"sector": 42}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 42, "planets": []}})

    client.land_flow(ctx)

    assert ctx.state.get("on_planet") is not True
    assert "did not return the requested planet ID" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]


def test_planet_landing_rejects_boolean_id_in_success_response(
    ctx_factory, fake_conn, capsys
):
    """A malformed JSON boolean must not be coerced to planet ID 1."""
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 1, "name": "Listed"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": True}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"sector": 42}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 42, "planets": [{"id": 1}]}})

    client.land_flow(ctx)

    assert ctx.state.get("on_planet") is not True
    assert ctx.state.get("current_planet_id") is None
    assert "did not return the requested planet ID" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]


def test_planet_landing_rejects_fractional_id_in_success_response(
    ctx_factory, fake_conn, capsys
):
    """A fractional JSON number must not be truncated into an object ID."""
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77.5}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"sector": 42}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 42, "planets": [{"id": 77}]}})

    client.land_flow(ctx)

    assert ctx.state.get("on_planet") is not True
    assert ctx.state.get("current_planet_id") is None
    assert "did not return the requested planet ID" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]


def test_planet_landing_rejects_boolean_id_as_malformed_metadata(
    ctx_factory, fake_conn, capsys
):
    """Python's bool-is-int coercion must not create a planet target."""
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": True, "name": "Malformed"}]})

    client.land_flow(ctx)

    assert fake_conn.calls == []
    assert ctx.state.get("on_planet") is not True
    assert "No listed planet has a usable server ID." in capsys.readouterr().out


def test_planet_landing_does_not_open_different_authoritative_planet(
    ctx_factory, fake_conn, capsys
):
    """A successful envelope must identify the planet the player selected."""
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 88}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"sector": 42}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9}}})
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 42, "planets": [{"id": 77, "name": "Private"}]},
    })

    client.land_flow(ctx)

    assert ctx.state.get("on_planet") is not True
    assert ctx.state.get("current_planet_id") is None
    assert "did not return the requested planet ID" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]


def test_planet_landing_refusal_preserves_existing_confirmed_surface_state(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    ctx.state.update({"on_planet": True, "current_planet_id": 66})
    fake_conn.queue("planet.land", {
        "status": "refused", "error": {"message": "Permission denied."},
    })
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 1, "sector": 42}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 42, "planets": [{"id": 77}]}})

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 66
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert "Permission denied." in capsys.readouterr().out


def test_zero_turn_landing_refusal_refreshes_without_claiming_surface(
    ctx_factory, fake_conn, capsys
):
    """A server refusal at zero turns must remain authoritative and recoverable."""
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    ctx.state.update({"on_planet": True, "current_planet_id": 66})
    fake_conn.queue("planet.land", {
        "status": "refused",
        "error": {"message": "You have no turns remaining.", "data": {
            "planet_id": 77,
            "landing_allowed": False,
            "turns_remaining": 0,
        }},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 1, "sector": 42, "turns": 0}},
    })
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9}}})
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 42, "planets": [{"id": 77}]},
    })

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 66
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "You have no turns remaining." in output
    assert "Landing failed: You have no turns remaining." in output


def test_planet_landing_refusal_reconciles_after_malformed_player_refresh(
    ctx_factory, fake_conn, capsys
):
    """A bad HUD envelope must not prevent the remaining authoritative reads."""
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    fake_conn.queue("planet.land", {
        "status": "refused", "error": {"message": "Permission denied."},
    })
    fake_conn.queue("player.my_info", [])
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9}}})
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 42, "planets": [{"id": 77}]},
    })

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert "Landing failed: Permission denied." in capsys.readouterr().out


def test_planet_landing_refusal_survives_malformed_sector_refresh(
    ctx_factory, fake_conn, capsys
):
    """A malformed reconciliation envelope must not crash or replace cached sector state."""
    original_sector = {"id": 42, "planets": [{"id": 77, "name": "Private"}]}
    ctx = ctx_factory(sector=original_sector)
    fake_conn.queue("planet.land", {
        "status": "refused", "error": {"message": "Permission denied."},
    })
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 1, "sector": 42}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9}}})
    fake_conn.queue("move.describe_sector", [])

    client.land_flow(ctx)

    assert ctx.last_sector_desc == original_sector
    output = capsys.readouterr().out
    assert "Landing failed: Permission denied." in output
    assert "Malformed server response" in output


def test_planet_landing_refusal_does_not_replace_player_from_other_session(
    ctx_factory, fake_conn, capsys
):
    """A refusal refresh must not merge a player record for another session."""
    ctx = ctx_factory(sector={"id": 42, "planets": [{"id": 77, "name": "Private"}]})
    ctx.player_info = {"player": {"id": 3, "sector": 42, "turns": 12}}
    fake_conn.queue("planet.land", {
        "status": "refused", "error": {"message": "Permission denied."},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 99, "sector": 42, "turns": 11}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"ship_id": 9, "onplanet": 0}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 42, "planets": [{"id": 77}]},
    })

    client.land_flow(ctx)

    assert ctx.player_info == {"player": {"id": 3, "sector": 42, "turns": 12}}
    assert ctx.state.get("on_planet") is not True
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert "Landing failed: Permission denied." in capsys.readouterr().out


def test_planet_attack_delegates_protection_and_loss_to_server(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory(sector={"id": 7, "planets": [{"id": 8, "name": "Safe target"}]})
    fake_conn.queue("combat.attack_planet", {"status": "refused", "error": {"message": "Captain Z intervenes"}})
    monkeypatch.setattr("builtins.input", lambda prompt: "1" if "number" in prompt else "y")
    client.combat_attack_planet_flow(ctx)
    assert fake_conn.calls[0] == {"command": "combat.attack_planet", "data": {"planet_id": 8}}
    assert "Captain Z intervenes" in capsys.readouterr().out

    ctx.last_sector_desc = {"id": 17, "planets": [{"id": 1, "name": "Terra"}]}
    fake_conn.queue("combat.attack_planet", {"status": "refused", "error": {"message": "Terra sanction"}})
    client.combat_attack_planet_flow(ctx)
    assert fake_conn.calls[-1] == {"command": "combat.attack_planet", "data": {"planet_id": 1}}
    assert "Terra sanction" in capsys.readouterr().out


def test_planet_attack_uses_listed_target_and_displays_server_counts(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 80, "name": "Nereid"}]})
    fake_conn.queue("combat.attack_planet", {"status": "ok", "type": "combat.attack_planet", "data": {
        "planet_id": 80, "attacker_remaining_fighters": 12,
        "defender_remaining_fighters": 0, "captured": True,
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 1, "sector": 17}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "fighters": 12}}})
    monkeypatch.setattr("builtins.input", lambda _prompt: "1" if "number" in _prompt else "y")

    client.combat_attack_planet_flow(ctx)

    out = capsys.readouterr().out
    assert "Captured: True" in out
    assert "Attacker fighters remaining: 12" in out
    assert "Planet fighters remaining: 0" in out
    assert fake_conn.calls[0] == {"command": "combat.attack_planet", "data": {"planet_id": 80}}


def test_planet_attack_without_canonical_response_type_refreshes_only(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 80, "name": "Nereid"}]})
    fake_conn.queue("combat.attack_planet", {"status": "ok", "data": {
        "planet_id": 80, "attacker_remaining_fighters": 12,
        "defender_remaining_fighters": 0, "captured": True,
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 1, "sector": 17}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "fighters": 12}}})
    monkeypatch.setattr("builtins.input", lambda _prompt: "1" if "number" in _prompt else "y")

    client.combat_attack_planet_flow(ctx)

    out = capsys.readouterr().out
    assert "unexpected response type" in out
    assert "Captured:" not in out
    assert [call["command"] for call in fake_conn.calls] == [
        "combat.attack_planet", "player.my_info", "ship.status", "move.describe_sector"
    ]


def test_planet_attack_loss_displays_authoritative_non_capture_result(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 80, "name": "Nereid"}]})
    fake_conn.queue("combat.attack_planet", {"status": "ok", "type": "combat.attack_planet", "data": {
        "planet_id": 80, "attacker_remaining_fighters": 0,
        "defender_remaining_fighters": 4, "captured": False,
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 1, "sector": 17}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "fighters": 0}}})
    monkeypatch.setattr("builtins.input", lambda _prompt: "1" if "number" in _prompt else "y")

    client.combat_attack_planet_flow(ctx)

    out = capsys.readouterr().out
    assert "Captured: False" in out
    assert "Attacker fighters remaining: 0" in out
    assert "Planet fighters remaining: 4" in out
    assert [call["command"] for call in fake_conn.calls] == [
        "combat.attack_planet", "player.my_info", "ship.status", "move.describe_sector"
    ]


def test_planet_attack_success_with_malformed_loss_capture_facts_refreshes_only(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 80, "name": "Nereid"}]})
    fake_conn.queue("combat.attack_planet", {"status": "ok", "data": {
        "planet_id": 80, "attacker_remaining_fighters": "0",
        "defender_remaining_fighters": 4, "captured": False,
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 1, "sector": 17}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "fighters": 0}}})
    monkeypatch.setattr("builtins.input", lambda _prompt: "1" if "number" in _prompt else "y")

    client.combat_attack_planet_flow(ctx)

    out = capsys.readouterr().out
    assert "not displayed" in out
    assert "Captured:" not in out
    assert [call["command"] for call in fake_conn.calls] == [
        "combat.attack_planet", "player.my_info", "ship.status", "move.describe_sector"
    ]


def test_planet_attack_refusal_refreshes_after_authoritative_defense_report(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 80, "name": "Nereid"}]})
    fake_conn.queue("combat.attack_planet", {"status": "refused", "error": {
        "message": "Planetary defenses repelled the attack.",
        "data": {
            "ship_remains_in_sector": True,
            "combat": [{"result": "repelled", "attacker_fighters_lost": 8,
                        "attacker_fighters_remaining": 2,
                        "planet_fighters_lost": 4,
                        "planet_fighters_remaining": 16,
                        "citadel_shields_absorbed": 12,
                        "citadel_shields_remaining": 0}],
        },
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {
        "player": {"id": 1, "sector": 17, "turns": 9}
    }})
    fake_conn.queue("ship.status", {"status": "ok", "data": {
        "ship": {"id": 9, "fighters": 2}
    }})
    monkeypatch.setattr("builtins.input", lambda _prompt: "1" if "number" in _prompt else "y")

    client.combat_attack_planet_flow(ctx)

    out = capsys.readouterr().out
    assert "Planetary fighters repelled" in out
    assert "8 fighters" in out
    assert [call["command"] for call in fake_conn.calls] == [
        "combat.attack_planet", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert ctx.player_info["player"]["turns"] == 9


def test_planet_attack_rejects_result_for_different_planet_and_refreshes(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 80, "name": "Nereid"}]})
    fake_conn.queue("combat.attack_planet", {"status": "ok", "data": {
        "planet_id": 81, "attacker_remaining_fighters": 12,
        "defender_remaining_fighters": 0, "captured": True,
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 1, "sector": 17}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "fighters": 12}}})
    monkeypatch.setattr("builtins.input", lambda _prompt: "1" if "number" in _prompt else "y")

    client.combat_attack_planet_flow(ctx)

    assert "no matching planet ID" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "combat.attack_planet", "player.my_info", "ship.status", "move.describe_sector",
    ]


def test_planet_attack_rejects_success_with_unexpected_response_type(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 80, "name": "Nereid"}]})
    fake_conn.queue("combat.attack_planet", {"status": "ok", "type": "planet.info", "data": {
        "planet_id": 80, "attacker_remaining_fighters": 12,
        "defender_remaining_fighters": 0, "captured": True,
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 1, "sector": 17}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "fighters": 12}}})
    monkeypatch.setattr("builtins.input", lambda _prompt: "1" if "number" in _prompt else "y")

    client.combat_attack_planet_flow(ctx)

    assert "unexpected response type" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "combat.attack_planet", "player.my_info", "ship.status", "move.describe_sector",
    ]


def test_planet_attack_invalid_reply_reconciles_without_claiming_capture(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 80, "name": "Nereid"}]})
    fake_conn.queue("combat.attack_planet", None)
    fake_conn.queue("player.my_info", {"status": "ok", "data": {
        "player": {"id": 1, "sector": 17}
    }})
    fake_conn.queue("ship.status", {"status": "ok", "data": {
        "ship": {"id": 9, "fighters": 0}
    }})
    monkeypatch.setattr("builtins.input", lambda _prompt: "1" if "number" in _prompt else "y")

    client.combat_attack_planet_flow(ctx)

    out = capsys.readouterr().out
    assert "invalid response" in out
    assert "Captured:" not in out
    assert [call["command"] for call in fake_conn.calls] == [
        "combat.attack_planet", "player.my_info", "ship.status", "move.describe_sector",
    ]
