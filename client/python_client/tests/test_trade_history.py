"""Trade history is displayed from server records and paginated by server cursor."""
import client


def test_trade_history_displays_records_and_follows_returned_cursor(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("trade.history", {"status": "ok", "data": {
        "history": [{"id": 4, "action": "buy", "units": 8, "commodity": "ORE",
                     "price_per_unit": 35.5, "port_id": 18, "timestamp": 1790000000}],
        "next_cursor": "1789999999_3",
    }})
    fake_conn.queue("trade.history", {"status": "ok", "data": {
        "history": [{"id": 3, "action": "sell", "units": 8, "commodity": "ORE",
                     "price_per_unit": 42.0, "port_id": 17, "timestamp": 1789999999}],
    }})
    answers = iter(["y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.trade_history_flow(ctx)

    assert fake_conn.calls == [
        {"command": "trade.history", "data": {"limit": 20}},
        {"command": "trade.history", "data": {"limit": 20, "cursor": "1789999999_3"}},
    ]
    output = capsys.readouterr().out
    assert "buy 8 ORE" in output and "35.5 CR/unit" in output and "port 18" in output
    assert "sell 8 ORE" in output and "42.0 CR/unit" in output and "port 17" in output


def test_trade_history_stops_on_refusal_without_retry(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    fake_conn.queue("trade.history", {
        "status": "refused", "error": {"message": "Not authenticated."},
    })

    client.trade_history_flow(ctx)

    assert fake_conn.calls == [{"command": "trade.history", "data": {"limit": 20}}]
    assert "Not authenticated" in capsys.readouterr().out
