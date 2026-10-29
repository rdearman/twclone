import client


def test_autopilot_control_displays_server_confirmation_and_updates_state(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("move.autopilot.control", {
        "status": "ok",
        "type": "move.autopilot.controlled_v1",
        "data": {"action": "continue", "state": "running", "next_sector_id": 42},
    })
    inputs = iter(["n", "q"])
    monkeypatch.setattr("builtins.input", lambda *_: next(inputs))

    client.cli_autopilot_control(ctx)

    assert "Autopilot continue confirmed: running; next sector 42." in capsys.readouterr().out
    assert ctx.state["autopilot_status"]["next_sector_id"] == 42
    assert fake_conn.calls[-1] == {
        "command": "move.autopilot.control", "data": {"action": "continue"}
    }


def test_autopilot_control_surfaces_refusal_and_keeps_menu_available(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("move.autopilot.control", {
        "status": "refused", "error": {"message": "No resumable autopilot route"}
    })
    inputs = iter(["n", "q"])
    monkeypatch.setattr("builtins.input", lambda *_: next(inputs))

    client.cli_autopilot_control(ctx)

    assert "Autopilot continue refused: No resumable autopilot route" in capsys.readouterr().out
    assert "autopilot_status" not in ctx.state


def test_autopilot_status_surfaces_server_refusal(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    fake_conn.queue("move.autopilot.status", {
        "status": "error", "error": {"message": "No route saved"}
    })

    client.cli_autopilot_status(ctx)

    assert "Failed to get autopilot status: No route saved" in capsys.readouterr().out
    assert "autopilot_status" not in ctx.state


def test_autopilot_status_shows_authoritative_persisted_route_without_moving(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("move.autopilot.status", {
        "status": "ok",
        "type": "move.autopilot.status_v1",
        "data": {
            "state": "stopped", "mode": "manual", "current_sector_id": 41,
            "target_sector_id": 43, "next_sector_id": 42, "path": [41, 42, 43],
        },
    })
    monkeypatch.setattr("builtins.input", lambda *_: "")

    data = client.refresh_autopilot_status(ctx, announce=True)

    output = capsys.readouterr().out
    assert data["next_sector_id"] == 42
    assert "Saved autopilot route restored: stopped; next sector 42" in output
    assert "No movement will resume" in output
    assert [call["command"] for call in fake_conn.calls] == ["move.autopilot.status"]
    assert ctx.state["autopilot_status"] == data


def test_autopilot_stop_now_uses_documented_command_without_warp(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("move.autopilot.stop", {
        "status": "ok", "type": "move.autopilot.stopped_v1",
        "data": {"state": "stopped", "current_sector_id": 41},
    })

    assert client.cli_autopilot_stop(ctx) is True

    assert "Autopilot stopped at sector 41; no movement was made" in capsys.readouterr().out
    assert fake_conn.calls == [{"command": "move.autopilot.stop", "data": {}}]


def test_autopilot_stop_refusal_keeps_last_confirmed_state(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    previous = {"state": "running", "next_sector_id": 42}
    ctx.state["autopilot_status"] = previous
    fake_conn.queue("move.autopilot.stop", {
        "status": "refused", "error": {"message": "No saved route"}
    })

    assert client.cli_autopilot_stop(ctx) is False

    assert "Autopilot stop refused: No saved route" in capsys.readouterr().out
    assert ctx.state["autopilot_status"] == previous
    assert fake_conn.calls == [{"command": "move.autopilot.stop", "data": {}}]


def test_controls_menu_stop_now_calls_only_server_stop(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory()
    fake_conn.queue("move.autopilot.stop", {
        "status": "ok", "data": {"state": "stopped", "current_sector_id": 12}
    })
    inputs = iter(["s", "q"])
    monkeypatch.setattr("builtins.input", lambda *_: next(inputs))

    client.cli_autopilot_control(ctx)

    assert "Autopilot stopped at sector 12" in capsys.readouterr().out
    assert fake_conn.calls == [{"command": "move.autopilot.stop", "data": {}}]


def test_active_route_prompt_exposes_information_and_status_without_guessing_commands(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 41, "name": "Origin"})
    ctx.state["ap_route"] = [42, 43]
    fake_conn.queue("move.warp", {"status": "ok", "data": {"to_sector_id": 42}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"sector_id": 42, "name": "Middle"}})
    fake_conn.queue("sector.info", {"status": "ok", "data": {"sector_id": 42, "name": "Middle"}})
    fake_conn.queue("move.autopilot.status", {"status": "ok", "data": {"state": "running", "next_sector_id": 43}})
    fake_conn.queue("move.warp", {"status": "ok", "data": {"to_sector_id": 43}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"sector_id": 43, "name": "Destination"}})
    answers = iter(["i", "r", "s", "d", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))
    monkeypatch.setattr(client, "call_handler", lambda *_: True)

    client.start_autopilot(ctx)

    commands = [call["command"] for call in fake_conn.calls]
    assert commands.count("sector.info") == 1
    assert commands.count("move.autopilot.status") == 1
    assert "move.warp" in commands and ctx.state["ap_route"] == []
    assert "Choose Y, N, E, I, R, S, D or P." not in capsys.readouterr().out


def test_active_route_stop_and_stop_after_next_use_documented_server_controls(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 1})
    ctx.state["ap_route"] = [2, 3, 4]
    fake_conn.queue("move.warp", {"status": "ok", "data": {"to_sector_id": 2}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"sector_id": 2}})
    fake_conn.queue("move.autopilot.control", {"status": "ok", "data": {"action": "stop_at_next", "state": "running"}})
    fake_conn.queue("move.warp", {"status": "ok", "data": {"to_sector_id": 3}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"sector_id": 3}})
    monkeypatch.setattr("builtins.input", lambda *_: "p")

    client.start_autopilot(ctx)

    assert [call["command"] for call in fake_conn.calls].count("move.warp") == 2
    assert {"command": "move.autopilot.control", "data": {"action": "stop_at_next"}} in fake_conn.calls
    assert ctx.state["ap_route"] == [4]
    assert "stopped after the next confirmed sector" in capsys.readouterr().out


def test_saved_route_execute_prompt_accepts_express_mode(ctx_factory, fake_conn, monkeypatch):
    ctx = ctx_factory(sector={"id": 41, "name": "Origin"})
    fake_conn.queue("move.autopilot.start", {
        "status": "ok", "type": "move.autopilot.route_v1", "data": {"path": [41, 42]},
    })
    fake_conn.queue("move.autopilot.control", {
        "status": "ok", "data": {"action": "express", "state": "running"},
    })
    started = []
    answers = iter(["42", "y", "e"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))
    monkeypatch.setattr(client, "start_autopilot", lambda current: started.append(current))

    client.saved_autopilot_route_flow(ctx)

    assert started == [ctx]
    assert fake_conn.calls == [
        {"command": "move.autopilot.start", "data": {"to_sector_id": 42}},
        {"command": "move.autopilot.control", "data": {"action": "express"}},
    ]


def test_autopilot_status_renders_documented_path_and_next_alias(capsys):
    client.print_autopilot_status({
        "state": "running", "mode": "express", "current_sector_id": 10,
        "target_sector_id": 12, "next": 11, "path": [10, 11, 12],
        "last_error": "",
    })

    output = capsys.readouterr().out
    assert "running (express)" in output
    assert "current sector 10" in output and "destination 12" in output
    assert "next 11" in output and "path 10 → 11 → 12" in output


def test_saved_autopilot_route_uses_server_path_and_executes_only_after_confirmation(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 41, "name": "Origin"})
    fake_conn.queue("move.autopilot.start", {
        "status": "ok", "type": "move.autopilot.route_v1",
        "data": {"path": [41, 42, 43], "hops": 2},
    })
    started = []
    answers = iter(["43", "y", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))
    monkeypatch.setattr(client, "start_autopilot", lambda current: started.append(current))

    client.saved_autopilot_route_flow(ctx)

    assert fake_conn.calls == [{"command": "move.autopilot.start", "data": {"to_sector_id": 43}}]
    assert ctx.state["ap_route"] == [42, 43]
    assert started == [ctx]
    assert "41 → 42 → 43" in capsys.readouterr().out


def test_declining_saved_route_execution_stops_server_route_without_warp(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 41, "name": "Origin"})
    fake_conn.queue("move.autopilot.start", {
        "status": "ok", "type": "move.autopilot.route_v1", "data": {"path": [41, 42]},
    })
    fake_conn.queue("move.autopilot.stop", {
        "status": "ok", "type": "move.autopilot.stopped_v1", "data": {"state": "stopped"},
    })
    answers = iter(["42", "y", "n"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.saved_autopilot_route_flow(ctx)

    assert fake_conn.calls == [
        {"command": "move.autopilot.start", "data": {"to_sector_id": 42}},
        {"command": "move.autopilot.stop", "data": {}},
    ]
    assert "move.warp" not in [call["command"] for call in fake_conn.calls]
    assert ctx.state["ap_route"] == []
    assert "Autopilot stopped" in capsys.readouterr().out
