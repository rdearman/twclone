"""
Tests for corporation context hydration and the equity dividend flow.
"""
import pytest
import client


def _iter_options(menu: dict):
    if "sections" in menu:
        for sec in menu["sections"]:
            yield from sec.get("options", [])
    else:
        yield from menu.get("options", [])


def test_update_corp_context_never_calls_list_stocks_or_equity_list(ctx_factory, fake_conn):
    ctx = ctx_factory()
    fake_conn.queue("player.my_info", {
        "status": "ok",
        "data": {"player": {"id": 1, "corp_id": 10}}
    })
    fake_conn.queue("corp.status", {
        "status": "ok",
        "data": {"corp_id": 10, "your_role": "Leader"}
    })

    client._update_corp_context(ctx, force=True)

    calls = [c["command"] for c in fake_conn.calls]
    assert "stock.exchange.list_stocks" not in calls
    assert "equity.exchange.list" not in calls


def test_update_corp_context_hydrates_membership_and_ceo_officer_roles(ctx_factory, fake_conn):
    ctx = ctx_factory()
    fake_conn.queue("player.my_info", {
        "status": "ok",
        "data": {"player": {"id": 1, "corp_id": 10}}
    })
    fake_conn.queue("corp.status", {
        "status": "ok",
        "data": {"corp_id": 10, "your_role": "Leader"}
    })

    client._update_corp_context(ctx, force=True)

    assert ctx.state["in_corporation"] is True
    assert ctx.state["is_corp_member"] is True
    assert ctx.state["is_ceo"] is True
    assert ctx.state["is_ceo_or_officer"] is True


def test_corporation_roster_sends_server_required_corp_id(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    fake_conn.queue("corp.status", {"status": "ok", "data": {"corp_id": 10}})
    fake_conn.queue("corp.roster", {"status": "ok", "data": {"corp_id": 10, "roster": []}})

    client.corp_roster_flow(ctx)

    assert [(call["command"], call["data"]) for call in fake_conn.calls] == [
        ("corp.status", {}), ("corp.roster", {"corp_id": 10})
    ]
    assert "No members" in capsys.readouterr().out


def test_corporation_create_uses_only_published_name_field(ctx_factory, fake_conn, monkeypatch):
    ctx = ctx_factory()
    fake_conn.queue("corp.create", {"status": "ok", "data": {"corp_id": 10, "name": "Nova"}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 1, "corp_id": 10}}})
    fake_conn.queue("corp.status", {"status": "ok", "data": {"corp_id": 10, "your_role": "Leader"}})
    inputs = iter(["Nova"])
    monkeypatch.setattr("builtins.input", lambda prompt="": next(inputs))

    client.corp_create_flow(ctx)

    assert fake_conn.calls[0] == {"command": "corp.create", "data": {"name": "Nova"}}
    assert ctx.state["in_corporation"] and ctx.state["is_ceo"]


def test_corporation_context_reads_published_player_corp_id(ctx_factory, fake_conn):
    ctx = ctx_factory()
    ctx.player_info = {"player": {"corp_id": 10}}

    client._update_corp_context(ctx)

    assert ctx.state["in_corporation"] is True
    assert ctx.state["corp_id"] == 10


def test_corporation_roster_refuses_to_guess_missing_corp_id(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    fake_conn.queue("corp.status", {"status": "ok", "data": {}})

    client.corp_roster_flow(ctx)

    assert len(fake_conn.calls) == 1
    assert "valid corporation ID" in capsys.readouterr().out


def test_dividend_action_visible_to_ceo_in_normal_mode(ctx_factory, menus):
    ctx = ctx_factory(debug=False)
    ctx.state["is_ceo"] = True
    ctx.state["corp_is_public"] = True

    visible = [
        opt["key"] for opt in _iter_options(menus["EXCHANGE_MAIN"])
        if client._option_visible_with_ctx(ctx, opt)
    ]
    assert "d" in visible


def test_dividend_action_hidden_for_non_ceo_in_debug_mode(ctx_factory, menus):
    ctx = ctx_factory(debug=True)
    ctx.state["is_ceo"] = False
    ctx.state["corp_is_public"] = True

    visible = [
        opt["key"] for opt in _iter_options(menus["EXCHANGE_MAIN"])
        if client._option_visible_with_ctx(ctx, opt)
    ]
    assert "d" not in visible


def test_dividend_action_visible_for_ceo_regardless_of_public_flag(ctx_factory, menus):
    ctx = ctx_factory(debug=False)
    ctx.state["is_ceo"] = True
    ctx.state["corp_is_public"] = False

    visible = [
        opt["key"] for opt in _iter_options(menus["EXCHANGE_MAIN"])
        if client._option_visible_with_ctx(ctx, opt)
    ]
    assert "d" in visible


def test_stock_dividend_set_flow_sends_equity_dividend_set_without_stock_id_prompt(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory(debug=True)
    ctx.state["is_ceo"] = True
    fake_conn.queue("equity.dividend_set", {
        "status": "ok",
        "data": {
            "message": "Dividend declared successfully.",
            "equity_id": 5,
            "amount_per_share": 100,
            "total_payout": 10000
        }
    })

    inputs = iter(["100"])
    monkeypatch.setattr("builtins.input", lambda prompt="": next(inputs))

    client.stock_dividend_set_flow(ctx)

    out = capsys.readouterr().out
    assert "Enter Stock ID" not in out
    assert "Dividend declared successfully!" in out

    call = fake_conn.calls[-1]
    assert call["command"] == "equity.dividend_set"
    assert call["data"] == {"amount_per_share": 100}


def test_stock_dividend_set_flow_handles_server_refusal_without_traceback(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory(debug=True)
    ctx.state["is_ceo"] = True
    fake_conn.queue("equity.dividend_set", {
        "status": "error",
        "error": {"message": "Your corporation is not publicly traded."}
    })

    inputs = iter(["50"])
    monkeypatch.setattr("builtins.input", lambda prompt="": next(inputs))

    client.stock_dividend_set_flow(ctx)

    out = capsys.readouterr().out
    assert "[Error] Failed to declare dividend: Your corporation is not publicly traded." in out
    call = fake_conn.calls[-1]
    assert call["command"] == "equity.dividend_set"
    assert call["data"] == {"amount_per_share": 50}
