"""Client acceptance checks for V3-PLAN-01 landing refresh semantics."""

from dataclasses import replace

import client


class DisconnectingLandingConn:
    def __init__(self, delegate):
        self.delegate = delegate
        self.calls = delegate.calls

    def rpc(self, command, data):
        self.calls.append({"command": command, "data": data})
        if command == "planet.land":
            raise ConnectionError("socket closed")
        return self.delegate.rpc(command, data)


class DisconnectingLandingRefreshConn:
    def __init__(self, delegate, disconnect_on):
        self.delegate = delegate
        self.disconnect_on = disconnect_on
        self.calls = delegate.calls

    def rpc(self, command, data):
        if command == self.disconnect_on:
            self.calls.append({"command": command, "data": data})
            raise ConnectionError("socket closed during landing refresh")
        return self.delegate.rpc(command, data)


def test_landing_success_keeps_authoritative_turn_refresh_before_surface(
    ctx_factory, fake_conn, monkeypatch
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "ok",
        "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok",
        "data": {"player": {"id": 3, "turns": 39, "credits": 1200}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok",
        "data": {"ship": {"ship_id": 9, "hull": 100, "onplanet": 77}},
    })
    fake_conn.queue("planet.info", {
        "status": "ok",
        "data": {"planet": {"planet_id": 77, "sector_id": 17}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "")

    client.land_flow(ctx)

    assert ctx.player_info == {"player": {"id": 3, "turns": 39, "credits": 1200}}
    assert ctx.state["current_planet_id"] == 77
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "planet.info",
    ]


def test_landing_uses_refreshed_player_ship_identity_when_hud_ship_is_unknown(
    ctx_factory, fake_conn, capsys
):
    """A different ship must not authorize the surface when player.my_info names one."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "ship_id": 9, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 10, "hull": 100, "onplanet": 77}},
    })

    client.land_flow(ctx)

    assert ctx.state.get("on_planet") is not True
    assert ctx.state["current_planet_id"] == 77
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status",
    ]


def test_landing_rejects_success_shaped_player_refresh_with_wrong_response_type(
    ctx_factory, fake_conn, capsys
):
    """A player-shaped record from another endpoint cannot authorize landing."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "ship.status",
        "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "onplanet": 77}},
    })

    client.land_flow(ctx)

    assert ctx.state.get("on_planet") is not True
    assert ctx.menu_stack == ["MAIN"]
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out


def test_landing_rejects_success_shaped_ship_refresh_with_wrong_response_type(
    ctx_factory, fake_conn, capsys
):
    """A ship-shaped record from another endpoint cannot authorize landing."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "player.info",
        "data": {"ship": {"ship_id": 9, "onplanet": 77}},
    })

    client.land_flow(ctx)

    assert ctx.state.get("on_planet") is not True
    assert ctx.menu_stack == ["MAIN"]
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out


def test_landing_accepts_explicit_success_envelope_type(
    ctx_factory, fake_conn, monkeypatch
):
    """The authoritative success type is accepted when its planet ID matches."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "ok",
        "type": "planet.land.success",
        "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 100, "onplanet": 77}},
    })
    fake_conn.queue("planet.info", {
        "status": "ok", "data": {"planet": {"planet_id": 77, "sector_id": 17}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "")

    client.land_flow(ctx)

    assert ctx.state["current_planet_id"] == 77
    assert ctx.state["on_planet"] is True
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "planet.info",
    ]


def test_landing_refresh_disconnect_preserves_confirmation_without_crashing(
    ctx_factory, fake_conn, capsys
):
    """A lost post-landing HUD read must not open a stale surface or traceback."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    ctx.conn = DisconnectingLandingRefreshConn(fake_conn, "ship.status")

    client.land_flow(ctx)

    assert ctx.state["current_planet_id"] == 77
    assert ctx.state["on_planet"] is False
    assert ctx.menu_stack == ["MAIN"]
    assert "Landing state refresh could not be confirmed" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status",
    ]


def test_landing_success_with_unexpected_response_type_is_not_claimed(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "ok", "type": "planet.info", "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 40}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 100, "onplanet": 77}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"sector_id": 17, "planets": [{"id": 77}]},
    })

    client.land_flow(ctx)

    assert ctx.state.get("on_planet") is not True
    assert ctx.state.get("current_planet_id") is None
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert "unexpected type planet.info" in capsys.readouterr().out


def test_landing_does_not_open_surface_for_contradictory_ship_planet_id(
    ctx_factory, fake_conn, capsys
):
    """A refreshed active ship must still identify the requested planet."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 9, "onplanet": 88}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77}]},
    })

    client.land_flow(ctx)

    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack == ["MAIN"]
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out


def test_landing_refusal_retains_authoritative_turn_refresh_without_claiming_planet(
    ctx_factory, fake_conn
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "refused",
        "error": {"message": "Planetary defenses refused landing."},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok",
        "data": {"player": {"id": 3, "turns": 40, "credits": 1200}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok",
        "data": {"ship": {"ship_id": 9, "hull": 85}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "data": {"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    })

    client.land_flow(ctx)

    assert ctx.player_info == {"player": {"id": 3, "turns": 40, "credits": 1200}}
    assert "current_planet_id" not in ctx.state
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]


def test_malformed_landing_success_handles_disconnect_during_reconciliation(
    ctx_factory, fake_conn, capsys
):
    """A malformed success must not traceback if its safety refresh disconnects."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"message": "Landed"}})
    ctx.conn = DisconnectingLandingRefreshConn(fake_conn, "player.my_info")

    client.land_flow(ctx)

    assert ctx.state.get("current_planet_id") is None
    assert ctx.state.get("on_planet") is not True
    output = capsys.readouterr().out
    assert "did not return the requested planet ID" in output
    assert "Landing state refresh could not be confirmed" in output
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]


def test_landing_refusal_handles_disconnect_during_sector_reconciliation(
    ctx_factory, fake_conn, capsys
):
    """A refused landing must remain safe when the final sector refresh is lost."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "refused", "error": {"message": "Landing is not permitted."},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 40}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 85}},
    })
    ctx.conn = DisconnectingLandingRefreshConn(fake_conn, "move.describe_sector")

    client.land_flow(ctx)

    assert ctx.state.get("current_planet_id") is None
    assert ctx.state.get("on_planet") is not True
    output = capsys.readouterr().out
    assert "Landing failed: Landing is not permitted." in output
    assert "Landing state refresh could not be confirmed" in output
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]


def test_landing_refusal_ignores_contradictory_nested_success_data(
    ctx_factory, fake_conn, capsys
):
    """A refused envelope cannot claim a planet from attached error data."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 12
    fake_conn.queue("planet.land", {
        "status": "refused",
        "error": {
            "message": "Planetary defenses refused landing.",
            "data": {"planet_id": 77, "landing_allowed": True},
        },
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

    assert ctx.state["current_planet_id"] == 12
    assert ctx.state["on_planet"] is True
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert "Landing failed: Planetary defenses refused landing." in capsys.readouterr().out


def test_confirmed_landing_keeps_only_id_when_surface_read_is_refused(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A refused detail read must not replace a confirmed landing with guessed data."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "ok", "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 100, "onplanet": 77}},
    })
    fake_conn.queue("planet.info", {
        "status": "refused", "error": {"message": "Planet details are not available."},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "")

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    output = capsys.readouterr().out
    assert "Planet details are not available." in output
    assert "Name: Terra" not in output
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "planet.info",
        "move.describe_sector",
    ]


def test_confirmed_landing_does_not_open_surface_when_hud_refresh_is_incomplete(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A confirmed ID is retained, but stale HUD state cannot open the surface."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "refused", "error": {"message": "Ship state unavailable."},
    })

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is False
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack == ["MAIN"]
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status",
    ]


def test_landing_does_not_open_surface_for_a_different_authoritative_ship(
    ctx_factory, fake_conn, capsys
):
    """A mismatched ship refresh is not a usable post-landing HUD refresh."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.hud = replace(ctx.hud, ship_id=9)
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 10, "hull": 100}},
    })

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is False
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack == ["MAIN"]
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out


def test_landing_does_not_replace_player_state_when_refresh_record_is_malformed(
    ctx_factory, fake_conn, capsys
):
    """A malformed post-landing player read cannot become authoritative state."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    confirmed_player = {"player": {"id": 3, "turns": 40, "credits": 1200}}
    ctx.player_info = confirmed_player.copy()
    fake_conn.queue("planet.land", {
        "status": "ok", "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"message": "landed"},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 100}},
    })

    client.land_flow(ctx)

    assert ctx.player_info == confirmed_player
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack == ["MAIN"]
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status",
    ]


def test_confirmed_landing_removes_surface_when_planet_refresh_is_refused(
    ctx_factory, fake_conn, capsys
):
    """A confirmed landing must not leave stale surface actions actionable."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 100, "onplanet": 77}},
    })
    fake_conn.queue("planet.info", {
        "status": "refused", "error": {"message": "Planet is no longer available."},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    })

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack == ["MAIN"]
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "planet.info",
        "move.describe_sector",
    ]


def test_landing_does_not_open_surface_without_authoritative_onplanet_identity(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A ship-only refresh cannot prove that landing completed on this planet."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 100}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "")

    client.land_flow(ctx)

    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack == ["MAIN"]
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status",
    ]


def test_landing_disconnect_does_not_claim_planet_or_retry_locally(
    ctx_factory, fake_conn, capsys
):
    """A lost landing reply remains uncertain until reconnect reconciliation."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.conn = DisconnectingLandingConn(fake_conn)

    client.land_flow(ctx)

    output = capsys.readouterr().out
    assert "Landing outcome is uncertain after connection loss" in output
    assert "Reconnect before attempting another landing" in output
    assert ctx.state.get("on_planet") is not True
    assert "current_planet_id" not in ctx.state
    assert [call["command"] for call in ctx.conn.calls] == ["planet.land"]
