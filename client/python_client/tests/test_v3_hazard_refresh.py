"""V3-HAZ-01 client acceptance for launch hazard reconciliation."""

import client
from hud import HudState


def test_ship_refresh_without_prior_id_stays_unavailable_without_authoritative_id(
    ctx_factory, fake_conn, capsys
):
    """A post-hazard ship read must identify the ship before updating the HUD."""
    ctx = ctx_factory(sector={"id": 17, "name": "Current"})
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"hull": 12, "shields": 0}},
    })

    client._refresh_ship_after_move(ctx)

    assert ctx.hud.ship_id is None
    assert ctx.hud.hull is None
    assert "did not identify the confirmed active ship" in capsys.readouterr().out


def test_successful_warp_hazard_refresh_does_not_accept_another_player(
    ctx_factory, fake_conn, capsys
):
    """Movement reconciliation must keep turns/credits bound to this player."""
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    ctx.player_info = {"player": {"id": 3}}
    ctx.hud = HudState(
        sector_id=17, ship_id=9, turns_remaining=40, credits=100, hull=72
    )
    fake_conn.queue("move.warp", {"status": "ok", "data": {"sector_id": 18}})
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"sector_id": 18, "name": "After"},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 44, "turns_remaining": 1, "credits": 999}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 9, "hull": 61}},
    })

    assert client._perform_warp(ctx, 18)

    assert ctx.hud.sector_id == 18
    assert ctx.hud.hull == 61
    assert ctx.hud.turns_remaining == 40
    assert ctx.hud.credits == 100
    assert "did not identify the logged-in player" in capsys.readouterr().out


def test_launch_refusal_with_error_hazard_refreshes_all_affected_domains(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "name": "Current"})
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.launch", {
        "status": "refused",
        "data": {"hazards": []},
        "error": {
            "message": "Launch stopped by entry hazards.",
            "data": {
                "hazards": [{"hazard_type": "minefield", "hull_lost": 12}],
                "ship_destroyed": False,
            },
        },
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 38}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 9, "hull": 88}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77}]},
    })

    client.launch_flow(ctx)

    output = capsys.readouterr().out
    assert "minefield" in output and "hull lost: 12" in output
    assert "Launch failed: Launch stopped by entry hazards." in output
    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.launch", "player.my_info", "ship.status", "move.describe_sector",
    ]


def test_successful_launch_without_ship_identity_keeps_surface_actionable_state_closed(
    ctx_factory, fake_conn, capsys
):
    """A launch must not expose space controls from an unidentified ship read."""
    ctx = ctx_factory(sector={"id": 17, "name": "Current"})
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.launch", {
        "status": "ok", "data": {"sector_id": 17},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 38}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"hull": 88}},
    })

    client.launch_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.menu_stack == ["MAIN", "SURFACE"]
    assert "post-launch HUD refresh was incomplete" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.launch", "player.my_info", "ship.status",
    ]


def test_launch_disconnect_preserves_confirmed_surface_until_reconnect(
    ctx_factory, fake_conn, capsys
):
    """A lost launch reply must not invent departure or retry a hazard path."""
    class DisconnectingConn:
        def __init__(self, delegate):
            self.delegate = delegate
            self.calls = delegate.calls

        def rpc(self, command, data):
            self.calls.append({"command": command, "data": data})
            if command == "planet.launch":
                raise ConnectionError("socket closed")
            return self.delegate.rpc(command, data)

    ctx = ctx_factory(sector={"id": 17, "name": "Current"})
    ctx.conn = DisconnectingConn(fake_conn)
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.menu_stack.append("SURFACE")

    client.launch_flow(ctx)

    output = capsys.readouterr().out
    assert "Launch outcome is uncertain after connection loss" in output
    assert "Reconnect before attempting another launch" in output
    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack == ["MAIN", "SURFACE"]
    assert ctx.state["last_rpc"] is None


def test_successful_launch_keeps_surface_until_sector_refresh_is_confirmed(
    ctx_factory, fake_conn, capsys
):
    """A lost post-launch sector read must not make an unconfirmed departure actionable."""
    class SectorDisconnectingConn:
        def __init__(self, delegate):
            self.delegate = delegate
            self.calls = delegate.calls

        def rpc(self, command, data):
            if command == "move.describe_sector":
                self.calls.append({"command": command, "data": data})
                raise ConnectionError("sector refresh lost")
            return self.delegate.rpc(command, data)

    ctx = ctx_factory(sector={"id": 17, "name": "Current"})
    ctx.conn = SectorDisconnectingConn(fake_conn)
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.hud = HudState(ship_id=9)
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.launch", {
        "status": "ok", "data": {"sector_id": 17},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 38}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 9, "hull": 88}},
    })

    client.launch_flow(ctx)

    output = capsys.readouterr().out
    assert "current sector could not be refreshed" in output
    assert ctx.state["on_planet"] is True
    assert ctx.menu_stack == ["MAIN", "SURFACE"]
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.launch", "player.my_info", "ship.status", "move.describe_sector",
    ]


def test_successful_launch_refresh_disconnect_is_controlled(
    ctx_factory, fake_conn, capsys
):
    """A lost success refresh must not turn a confirmed hazard outcome into a traceback."""
    class DisconnectingConn:
        def __init__(self, delegate):
            self.delegate = delegate
            self.calls = delegate.calls

        def rpc(self, command, data):
            self.calls.append({"command": command, "data": data})
            if command == "planet.launch":
                return {
                    "status": "ok",
                    "type": "planet.launch.success",
                    "data": {"sector_id": 17},
                }
            raise ConnectionError("socket closed during launch refresh")

    ctx = ctx_factory(sector={"id": 17, "name": "Current"})
    ctx.conn = DisconnectingConn(fake_conn)
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.menu_stack.append("SURFACE")

    client.launch_flow(ctx)

    output = capsys.readouterr().out
    assert "post-launch HUD refresh was incomplete" in output
    assert ctx.state["on_planet"] is True
    assert ctx.menu_stack == ["MAIN", "SURFACE"]
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.launch", "player.my_info", "ship.status",
    ]


def test_launch_refusal_refresh_disconnect_is_controlled(
    ctx_factory, fake_conn, capsys
):
    """A hazard refusal must remain usable when reconciliation loses its socket."""
    class DisconnectingConn:
        def __init__(self, delegate):
            self.delegate = delegate
            self.calls = delegate.calls

        def rpc(self, command, data):
            self.calls.append({"command": command, "data": data})
            if command == "planet.launch":
                return {
                    "status": "refused",
                    "error": {
                        "message": "Entry hazard stopped launch.",
                        "data": {"hazards": [{"hazard_type": "minefield", "hull_lost": 7}]},
                    },
                }
            raise ConnectionError("socket closed during refresh")

    ctx = ctx_factory(sector={"id": 17, "name": "Current"})
    ctx.conn = DisconnectingConn(fake_conn)
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.menu_stack.append("SURFACE")

    client.launch_flow(ctx)

    output = capsys.readouterr().out
    assert "minefield" in output and "hull lost: 7" in output
    assert "Launch failed: Entry hazard stopped launch." in output
    assert "Landing state refresh could not be confirmed after connection loss" in output
    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack == ["MAIN", "SURFACE"]
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.launch", "player.my_info", "ship.status", "move.describe_sector",
    ]


def test_launch_success_does_not_close_surface_for_a_different_authoritative_ship(
    ctx_factory, fake_conn, capsys
):
    """A hazard refresh for another ship must not make departure actionable."""
    ctx = ctx_factory(sector={"id": 17, "name": "Current"})
    ctx.hud = ctx.hud.__class__(ship_id=9, hull=72)
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.launch", {
        "status": "ok", "data": {"sector_id": 17},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 38}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 10, "hull": 20}},
    })

    client.launch_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack == ["MAIN", "SURFACE"]
    assert "post-launch HUD refresh was incomplete" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.launch", "player.my_info", "ship.status",
    ]


def test_launch_success_does_not_close_surface_for_a_different_authoritative_player(
    ctx_factory, fake_conn, capsys
):
    """A refresh for another account cannot reconcile a hazard launch."""
    ctx = ctx_factory(sector={"id": 17, "name": "Current"})
    ctx.player_info = {"player": {"id": 3}}
    ctx.hud = ctx.hud.__class__(ship_id=9, hull=72)
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.launch", {
        "status": "ok", "data": {"sector_id": 17},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 44, "turns": 38}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 9, "hull": 20}},
    })

    client.launch_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack == ["MAIN", "SURFACE"]
    assert "post-launch HUD refresh was incomplete" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.launch", "player.my_info", "ship.status",
    ]


def test_empty_success_hazards_do_not_hide_error_terminal_state():
    lines = client.present_move_hazards({
        "status": "error",
        "data": {"hazards": []},
        "error": {"data": {"ship_destroyed": True}},
    })

    assert lines == ["Movement outcome · ship destroyed: True"]


def test_warp_refusal_with_authoritative_hazards_refreshes_location_player_and_ship(
    ctx_factory, fake_conn, capsys, monkeypatch
):
    """A refusal carrying entry-hazard data must not be treated as side-effect free."""
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    monkeypatch.setattr("builtins.input", lambda *_: "44")
    fake_conn.queue("move.warp", {
        "status": "refused",
        "error": {
            "message": "Entry hazard stopped movement.",
            "data": {
                "hazards": [{"hazard_type": "fighter", "hull_lost": 9}],
                "ship_destroyed": False,
            },
        },
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "name": "Confirmed"},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 38}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 91}},
    })

    client.warp_flow(ctx)

    output = capsys.readouterr().out
    assert "fighter" in output and "hull lost: 9" in output
    assert "Entry hazard stopped movement." in output
    assert ctx.last_sector_desc["id"] == 17
    assert [call["command"] for call in fake_conn.calls] == [
        "move.warp", "move.describe_sector", "player.my_info", "ship.status",
    ]


def test_transwarp_success_refreshes_authoritative_state_before_rendering_destination(
    ctx_factory, fake_conn, capsys, monkeypatch
):
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    monkeypatch.setattr("builtins.input", lambda *_: "44")
    fake_conn.queue("move.transwarp", {
        "status": "ok",
        "data": {
            "to_sector_id": 44,
            "hazards": [{"hazard_type": "minefield", "hull_lost": 12}],
        },
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 44, "name": "Destination"},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 37}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 88}},
    })

    client.transwarp_flow(ctx)

    output = capsys.readouterr().out
    assert "minefield" in output and "hull lost: 12" in output
    assert "Transwarped to sector 44." in output
    assert ctx.last_sector_desc["id"] == 44
    assert [call["command"] for call in fake_conn.calls] == [
        "move.transwarp", "move.describe_sector", "player.my_info", "ship.status",
    ]


def test_successful_warp_with_lost_post_hazard_refresh_fails_closed(
    ctx_factory, fake_conn, capsys, monkeypatch
):
    """A committed move must not crash or render a guessed destination."""
    class DisconnectingConn:
        def __init__(self, delegate):
            self.delegate = delegate
            self.calls = delegate.calls

        def rpc(self, command, data):
            self.calls.append({"command": command, "data": data})
            if command == "move.describe_sector":
                raise ConnectionError("socket closed during authoritative refresh")
            return self.delegate.rpc(command, data)

    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    ctx.conn = DisconnectingConn(fake_conn)
    monkeypatch.setattr("builtins.input", lambda *_: "44")
    fake_conn.queue("move.warp", {
        "status": "ok", "data": {"sector_id": 44,
                                  "hazards": [{"hazard_type": "minefield"}]},
    })

    client.warp_flow(ctx)

    output = capsys.readouterr().out
    assert "minefield" in output
    assert "post-move state could not be confirmed after connection loss" in output
    assert "Moved to sector 44." not in output
    assert ctx.state["last_rpc"] is None
    assert ctx.last_sector_desc == {"id": 17, "name": "Before"}
    assert [call["command"] for call in fake_conn.calls] == [
        "move.warp", "move.warp", "move.describe_sector",
    ]


def test_hazard_refresh_does_not_merge_status_for_a_different_confirmed_ship(
    ctx_factory, fake_conn, capsys, monkeypatch
):
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    ctx.hud = ctx.hud.__class__(ship_id=9, hull=72)
    monkeypatch.setattr("builtins.input", lambda *_: "44")
    fake_conn.queue("move.warp", {
        "status": "ok", "data": {"to_sector_id": 44,
                                    "hazards": [{"hazard_type": "minefield", "hull_lost": 8}]},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 44, "name": "Destination"},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 37}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 12, "hull": 20}},
    })

    client.warp_flow(ctx)

    output = capsys.readouterr().out
    assert "minefield" in output
    assert "confirmed active ship" in output
    assert ctx.hud.ship_id == 9 and ctx.hud.hull == 72
