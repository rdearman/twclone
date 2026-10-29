"""Client-side access to the existing Ferengi trader and deal APIs."""
import client


def _listing(sector=27, traders=None, offers=None):
    return {"status": "ok", "type": "ferengi.traders_v1", "data": {
        "sector_id": sector,
        "traders": traders or [],
        "offers": offers or [],
    }}


def _offer(deal_id=91):
    return {
        "deal_id": deal_id, "trader_id": 3, "trader_code": "gaila",
        "display_name": "Gaila", "commodity": "ORE", "side": "trader_sells",
        "quantity": 5, "unit_price": 12, "status": "open",
        "expires_at": "later", "trader_sector_id": 28,
    }


def test_computer_menu_exposes_current_sector_traders(menus):
    options = {item["key"]: item["action"] for item in menus["COMPUTER"]["options"]}
    assert options["e"] == {"pycall": "ferengi_traders_flow"}


def test_trader_flow_lists_local_traders_and_outstanding_offer(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory(sector={"id": 27})
    fake_conn.queue("ferengi.traders", _listing(
        traders=[{"trader_id": 3, "trader_code": "gaila", "display_name": "Gaila", "reputation": 4}],
        offers=[_offer()],
    ))
    monkeypatch.setattr("builtins.input", lambda *_: "")

    client.ferengi_traders_flow(ctx)

    out = capsys.readouterr().out
    assert "Gaila · gaila · trader 3 · reputation 4" in out
    assert "Deal 91: Gaila · you buy 5 ORE at 12 CR each" in out
    assert "trader sector 28" in out
    assert fake_conn.calls == [{"command": "ferengi.traders", "data": {}}]


def test_trader_flow_accepts_only_listed_offer_then_refreshes(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory()
    fake_conn.queue("ferengi.traders", _listing(offers=[_offer()]))
    fake_conn.queue("ferengi.deal.accept", {"status": "ok", "type": "ferengi.deal.accepted_v1", "data": {
        "deal_id": 91, "status": "settled", "commodity": "ORE", "quantity": 5,
        "unit_price": 12, "reputation": 5,
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 4, "corp_id": None}}})
    fake_conn.queue("ferengi.traders", _listing())
    answers = iter(["1", "a"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.ferengi_traders_flow(ctx)

    assert fake_conn.calls == [
        {"command": "ferengi.traders", "data": {}},
        {"command": "ferengi.deal.accept", "data": {"deal_id": 91}},
        {"command": "player.my_info", "data": {}},
        {"command": "ship.status", "data": {}},
        {"command": "ferengi.traders", "data": {}},
    ]
    out = capsys.readouterr().out
    assert "Deal settled: 5 ORE at 12 CR each" in out
    assert "No open offers." in out


def test_trader_flow_rejects_server_refusal_without_replaying_action(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory()
    fake_conn.queue("ferengi.traders", _listing(offers=[_offer()]))
    fake_conn.queue("ferengi.deal.reject", {"status": "error", "error": {"message": "Offer expired."}})
    answers = iter(["1", "r"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.ferengi_traders_flow(ctx)

    assert fake_conn.calls == [
        {"command": "ferengi.traders", "data": {}},
        {"command": "ferengi.deal.reject", "data": {"deal_id": 91}},
    ]
    assert "Offer expired." in capsys.readouterr().out


def test_trader_flow_rejects_invalid_offer_selection_without_mutation(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory()
    fake_conn.queue("ferengi.traders", _listing(offers=[_offer()]))
    monkeypatch.setattr("builtins.input", lambda *_: "2")

    client.ferengi_traders_flow(ctx)

    assert fake_conn.calls == [{"command": "ferengi.traders", "data": {}}]
    assert "no deal action was sent" in capsys.readouterr().out


def test_trader_flow_handles_no_local_traders_or_open_offers(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory()
    fake_conn.queue("ferengi.traders", _listing())
    client.ferengi_traders_flow(ctx)
    out = capsys.readouterr().out
    assert "No active Ferengi traders" in out
    assert "No open offers." in out
    assert len(fake_conn.calls) == 1


def test_malformed_offer_never_changes_which_deal_is_selected(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory()
    fake_conn.queue("ferengi.traders", _listing(offers=[None, _offer()]))
    monkeypatch.setattr("builtins.input", lambda *_: "1")

    client.ferengi_traders_flow(ctx)

    assert fake_conn.calls == [{"command": "ferengi.traders", "data": {}}]
    assert "invalid trader listing" in capsys.readouterr().out
