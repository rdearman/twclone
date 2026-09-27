"""Navigation UI sends the documented pathfind shape and respects warp replies."""
import client


def test_pathfind_uses_only_schema_fields_and_displays_server_route(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 2271, "ships": []})
    fake_conn.queue("move.pathfind", {
        "status": "ok", "data": {"steps": [2271, 20, 30], "total_cost": 2},
    })
    inputs = iter(["30", "n"])
    monkeypatch.setattr("builtins.input", lambda *_: next(inputs))

    client.pathfind_flow(ctx)

    assert fake_conn.calls[0] == {
        "command": "move.pathfind",
        "data": {"from_sector_id": 2271, "to_sector_id": 30},
    }
    out = capsys.readouterr().out
    assert "2271 → 20 → 30" in out
    assert "hops: 2" in out


def test_autopilot_stops_on_first_refused_warp_and_keeps_remaining_route(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 2271, "ships": []})
    ctx.state["ap_route"] = [20, 30, 40]
    ctx.state["ap_route_plotted"] = True
    fake_conn.queue("move.warp", {
        "status": "refused", "error": {"message": "Insufficient turns."},
    })

    client.start_autopilot(ctx)

    assert fake_conn.calls == [{
        "command": "move.warp", "data": {"to_sector_id": 20},
    }]
    assert ctx.state["ap_route"] == [20, 30, 40]
    assert ctx.state["ap_route_plotted"] is True
    out = capsys.readouterr().out
    assert "Insufficient turns" in out
    assert "Autopilot stopped" in out


def test_trade_route_display_includes_server_search_metadata(ctx_factory, capsys):
    ctx = ctx_factory()
    ctx.state["last_rpc"] = {
        "status": "ok",
        "data": {
            "pathing_model": "full_graph",
            "pairs_checked": 45,
            "truncated": True,
            "routes": [],
        },
    }

    client.pretty_print_trade_routes(ctx)

    out = capsys.readouterr().out
    assert "Pathing model: full_graph" in out
    assert "Port pairs checked: 45" in out
    assert "Results were limited" in out


def test_trade_route_menu_exposes_supported_distance_filter(menus):
    option = next(
        option
        for option in menus["COMPUTER"]["options"]
        if option.get("key") == "l"
    )
    assert option["action"]["rpc"]["data"]["max_hops_from_player"] == (
        "<prompt:int:Max hops from current sector: [20]>"
    )
