"""Focused tests for the quoted dock trading workflow."""

import uuid

import client


PORT_RESPONSE = {
    "status": "ok",
    "data": {"port": {"id": 7, "name": "New Dawn", "commodities": [
        {"code": "ORE", "quantity": 12, "max_quantity": 100, "price": 40},
        {"code": "ORG", "quantity": 100, "max_quantity": 100, "price": 25},
    ]}},
}


def test_port_info_normalizes_stock_capacity_and_availability():
    normalized = client.normalize_port_info(PORT_RESPONSE)
    assert normalized["name"] == "New Dawn"
    assert normalized["commodities"] == [
        {"commodity": "ORE", "available": 12, "max_quantity": 100,
         "base_price": 40, "sells": True, "buys": True},
        {"commodity": "ORG", "available": 100, "max_quantity": 100,
         "base_price": 25, "sells": True, "buys": False},
    ]


def test_port_summary_uses_indicative_prices_and_port_name():
    lines = client.render_port_summary(client.normalize_port_info(PORT_RESPONSE))
    assert lines[0] == "--- Port: New Dawn ---"
    assert "ORE: stock 12/100; sell/buy; base ~40" in lines[1]


def test_cancelled_buy_quotes_but_does_not_mutate(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 2,
        "buy_price": 40.0, "total_buy_price": 80,
    }})
    monkeypatch.setattr("builtins.input", lambda prompt: {"Commodity number: ": "1",
                                                            "Quantity of ORE: ": "2",
                                                            "Commit this trade? [y/N]: ": ""}[prompt])
    client.dock_trade_flow(ctx, "buy")
    assert [c["command"] for c in fake_conn.calls] == ["trade.quote"]
    assert "cancelled" in capsys.readouterr().out.lower()


def test_confirmed_buy_uses_uuid_and_refreshes_state(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 2,
        "buy_price": 40.0, "total_buy_price": 80,
    }})
    fake_conn.queue("trade.buy", {"status": "ok", "data": {
        "total_cost": "82.00", "fees": "2.00", "credits_remaining": "18.00",
        "lines": [{"commodity": "ORE", "quantity": 2, "unit_price": "40.00"}],
    }})
    answers = iter(["1", "2", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))
    monkeypatch.setattr(uuid, "uuid4", lambda: uuid.UUID("12345678-1234-5678-1234-567812345678"))
    client.dock_trade_flow(ctx, "buy")
    calls = fake_conn.calls
    assert [c["command"] for c in calls] == ["trade.quote", "trade.buy", "player.my_info", "ship.status", "port.info"]
    assert calls[1]["data"]["idempotency_key"] == "12345678-1234-5678-1234-567812345678"
    assert "Total charged (including fees): 82" in capsys.readouterr().out


def test_sell_receipt_describes_net_total(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 2,
        "sell_price": 35.0, "total_sell_price": 70,
    }})
    fake_conn.queue("trade.sell", {"status": "ok", "data": {
        "total_cost": "68.00", "fees": "2.00", "credits_remaining": "68.00", "lines": []
    }})
    monkeypatch.setattr("builtins.input", lambda _prompt: {"Commodity number: ": "1",
                                                            "Quantity of ORE: ": "2",
                                                            "Commit this trade? [y/N]: ": "y"}[_prompt])
    client.dock_trade_flow(ctx, "sell")
    assert "Net credits received (after fees): 68" in capsys.readouterr().out
