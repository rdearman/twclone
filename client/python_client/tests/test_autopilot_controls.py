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
