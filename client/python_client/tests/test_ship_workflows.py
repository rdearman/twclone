"""Ship and StarDock flows use only server-listed hardware and hull contracts."""
import client


def test_hardware_purchase_selects_catalogue_item_and_refreshes_ship(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("hardware.list", {"status": "ok", "data": {"hardware": [
        {"item_code": "SHIELDS", "name": "Shield generator", "price": 100},
        {"item_code": "TRANSWARP", "name": "Transwarp drive", "price": 500},
    ]}})
    fake_conn.queue("hardware.buy", {"status": "ok", "data": {"quantity": 2, "item_code": "TRANSWARP"}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"credits": 900}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {
        "holds": 20, "fighters": 1, "shields": 2, "equipment": ["TRANSWARP"],
    }}})
    answers = iter(["2", "2"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.hardware_buy_flow(ctx)

    assert fake_conn.calls[0] == {"command": "hardware.list", "data": {}}
    assert fake_conn.calls[1] == {"command": "hardware.buy", "data": {
        "item_code": "TRANSWARP", "quantity": 2,
    }}
    assert [call["command"] for call in fake_conn.calls[2:]] == ["player.my_info", "ship.status"]
    assert "Successfully purchased 2 units of TRANSWARP" in capsys.readouterr().out


def test_hardware_purchase_rejects_item_not_in_server_catalogue(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("hardware.list", {"status": "ok", "data": {"hardware": [
        {"item_code": "SHIELDS", "name": "Shield generator", "price": 100},
    ]}})
    monkeypatch.setattr("builtins.input", lambda *_: "UNLISTED_DRIVE")

    client.hardware_buy_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["hardware.list"]
    assert "choose a hardware item from the server catalogue" in capsys.readouterr().out


def test_hardware_catalogue_refusal_is_presented_without_mutation(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("hardware.list", {
        "status": "refused", "error": {"message": "Dock at a supported hardware port."},
    })

    client.hardware_buy_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["hardware.list"]
    assert "Dock at a supported hardware port" in capsys.readouterr().out
