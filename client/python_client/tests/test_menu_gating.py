"""
Tests that normal-mode play never exposes debug-only or known-absent
commands, and that --debug correctly restores the retained diagnostic tools.

Requirements covered:
5. Normal mode excludes every debug-only or known-absent action.
6. Debug mode exposes the retained diagnostic actions.
7. No normal menu action sends a command known to be absent from the server
   registry.
"""
import client

# Commands confirmed absent from src/server_loop.c's command registry at the
# time of this audit. Any normal-mode-visible menu action referencing one of
# these is a bug.
KNOWN_ABSENT_COMMANDS = {
    "game.get_clock",
    "ship.set_primary",
    "move.intercept",
    "map.render",
}

# Registered RPC names whose current server handlers explicitly return
# ERR_NOT_IMPLEMENTED. Keep them out of ordinary player-facing menu paths.
KNOWN_SERVER_STUBS = {
    "insurance.policies.list",
    "insurance.policies.buy",
    "insurance.claim.file",
    "ship.sell",
    "ship.transfer",
    "ship.transfer_cargo",
    "ship.upgrade",
    "shipyard.sell",
}


def _iter_options(menu: dict):
    if "sections" in menu:
        for sec in menu["sections"]:
            yield from sec.get("options", [])
    else:
        yield from menu.get("options", [])


def _visible_options(menu: dict, ctx):
    return [
        opt for opt in _iter_options(menu)
        if client._option_visible_with_ctx(ctx, opt)
    ]


def _collect_rpc_commands(menus: dict, menu_id: str, ctx, seen=None):
    """Recursively collect rpc 'command' values reachable from menu_id given ctx."""
    if seen is None:
        seen = set()
    if menu_id in seen or menu_id not in menus:
        return set()
    seen.add(menu_id)
    commands = set()
    for opt in _visible_options(menus[menu_id], ctx):
        action = opt.get("action", {})
        if "rpc" in action:
            cmd = action["rpc"].get("command")
            if cmd:
                commands.add(cmd)
        if "submenu" in action:
            commands |= _collect_rpc_commands(menus, action["submenu"], ctx, seen)
    return commands


def test_normal_mode_excludes_debug_only_menu_entries(ctx_factory, menus):
    ctx = ctx_factory(debug=False)
    ctx.state["is_ceo"] = True

    main_keys = {o["key"] for o in _visible_options(menus["MAIN"], ctx)}
    assert "y" not in main_keys  # Testing Menu
    assert "b" not in main_keys  # Bulk Execute

    dock_keys = {o["key"] for o in _visible_options(menus["DOCK"], ctx)}
    assert "t" not in dock_keys  # raw JSON Trade

    move_keys = {o["key"] for o in _visible_options(menus["MOVE"], ctx)}
    assert "k" in move_keys  # Supported autopilot controls are normal play.

    computer_keys = {o["key"] for o in _visible_options(menus["COMPUTER"], ctx)}
    assert "g" in computer_keys  # Supported cargo transfer is available in normal play.

    exchange_keys = {o["key"] for o in _visible_options(menus["EXCHANGE_MAIN"], ctx)}
    assert "l" in exchange_keys  # Canonical equity exchange list.
    assert "p" in exchange_keys  # Canonical equity portfolio list.
    # CEO-only dividend controls are covered by an implemented equity flow.
    assert "d" in exchange_keys

    exchange_commands = {
        opt.get("action", {}).get("rpc", {}).get("command")
        for opt in _visible_options(menus["EXCHANGE_MAIN"], ctx)
    }
    assert "equity.exchange.list" in exchange_commands
    assert "equity.portfolio.list" in exchange_commands


def test_transwarp_requires_authoritative_current_sector(ctx_factory, menus):
    """NAV-02: do not expose a movement mutation without a server-owned ID."""
    stale_ctx = ctx_factory(debug=False)
    stale_ctx.last_sector_desc = {"name": "Unknown location"}
    stale_flags = client.compute_flags(stale_ctx)
    stale_keys = {
        o["key"] for o in _visible_options(menus["MOVE"], stale_ctx)
        if client.option_visible(o, stale_flags)
    }
    assert "t" not in stale_keys

    fresh_ctx = ctx_factory(debug=False, sector={"id": 17, "name": "Current"})
    fresh_flags = client.compute_flags(fresh_ctx)
    fresh_keys = {
        o["key"] for o in _visible_options(menus["MOVE"], fresh_ctx)
        if client.option_visible(o, fresh_flags)
    }
    assert "t" in fresh_keys


def test_debug_mode_exposes_retained_diagnostic_actions(ctx_factory, menus):
    ctx = ctx_factory(debug=True)

    main_keys = {o["key"] for o in _visible_options(menus["MAIN"], ctx)}
    assert "y" in main_keys
    assert "b" in main_keys

    dock_keys = {o["key"] for o in _visible_options(menus["DOCK"], ctx)}
    assert "t" in dock_keys

    move_keys = {o["key"] for o in _visible_options(menus["MOVE"], ctx)}
    assert "k" in move_keys

    computer_keys = {o["key"] for o in _visible_options(menus["COMPUTER"], ctx)}
    assert "g" in computer_keys

    exchange_keys = {o["key"] for o in _visible_options(menus["EXCHANGE_MAIN"], ctx)}
    assert "l" in exchange_keys
    assert "p" in exchange_keys


def test_dock_buy_sell_and_star_dock_bank_use_distinct_hotkeys(ctx_factory, menus):
    ctx = ctx_factory(debug=False)
    ctx.state.update({"is_stardock": True, "is_shipyard_port": True})

    options = _visible_options(menus["DOCK"], ctx)
    by_key = {option["key"]: option for option in options}

    assert len(by_key) == len(options)
    assert by_key["b"]["label"].startswith("(B) Buy")
    assert by_key["s"]["label"].startswith("(S) Sell")
    assert by_key["k"]["label"].startswith("(K) Bank")
    assert by_key["y"]["label"].startswith("(Y) Shipyard")


def test_bank_transfer_uses_authoritative_player_id_contract(menus):
    options = {item["key"]: item for item in menus["BANK"]["options"]}
    transfer = options["t"]["action"]["rpc"]
    assert transfer["command"] == "bank.transfer"
    assert transfer["data"] == {
        "to_player_id": "<prompt:int:Recipient player ID>",
        "amount": "<prompt:int:Amount>",
    }


def test_rankings_uses_authoritative_server_endpoint(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory()
    ctx.push("COMPUTER")
    fake_conn.queue("player.rankings", {"status": "ok", "data": {"rankings": [
        {"player_id": 7, "name": "Nova", "experience": 42, "alignment": 10},
    ]}})

    client.handle_choice(ctx, "k")

    assert "Nova" in capsys.readouterr().out
    assert fake_conn.calls == [{"command": "player.rankings", "data": {"by": "net_worth", "limit": 10}}]


def test_rankings_refusal_is_displayed_without_assuming_response_shape(
    ctx_factory, fake_conn, capsys
):
    fake_conn.queue("player.rankings", None)

    client.rankings_flow(ctx_factory())

    out = capsys.readouterr().out
    assert "Player rankings refused or failed:" in out
    assert fake_conn.calls == [{"command": "player.rankings", "data": {"by": "net_worth", "limit": 10}}]


def test_rankings_ignores_malformed_rows_without_claiming_success(
    ctx_factory, fake_conn, capsys
):
    fake_conn.queue("player.rankings", {
        "status": "ok",
        "data": {"rankings": [None, "not-a-ranking"]},
    })

    client.rankings_flow(ctx_factory())

    assert "No player rankings are available yet." in capsys.readouterr().out


def test_rankings_ignores_rows_without_authoritative_identity_or_types(
    ctx_factory, fake_conn, capsys
):
    fake_conn.queue("player.rankings", {
        "status": "ok",
        "data": {"rankings": [
            {"name": "Spoof", "experience": 99, "alignment": 1},
            {"player_id": 8, "name": "Rook", "experience": "18", "alignment": -3},
        ]},
    })

    client.rankings_flow(ctx_factory())

    assert "No player rankings are available yet." in capsys.readouterr().out


def test_no_normal_mode_rpc_action_uses_known_absent_command(ctx_factory, menus):
    ctx = ctx_factory(debug=False)

    # Only walk what's actually reachable from MAIN in normal mode; menus
    # gated behind --debug (e.g. TESTING, BULK) are excluded by the
    # show_if_ctx check inside _collect_rpc_commands before recursion.
    reachable_commands = _collect_rpc_commands(menus, "MAIN", ctx)

    absent_in_use = reachable_commands & KNOWN_ABSENT_COMMANDS
    assert absent_in_use == set(), f"Normal-mode menus reference absent commands: {absent_in_use}"


def test_normal_mode_does_not_expose_server_stub_ship_commands(ctx_factory, menus):
    ctx = ctx_factory(debug=False)
    reachable_commands = _collect_rpc_commands(menus, "MAIN", ctx)
    stub_commands = reachable_commands & KNOWN_SERVER_STUBS
    assert stub_commands == set(), f"Player menus expose server stubs: {stub_commands}"


def test_enter_ship_set_primary_hidden_and_disabled_without_debug(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory(
        sector={"id": 1, "ships": [{"id": 99, "owner": "derelict", "name": "Derelict"}]},
        debug=False,
    )
    inputs = iter(["", "p", "y", "q"])
    monkeypatch.setattr("builtins.input", lambda *_: next(inputs))

    client.enter_ship_menu(ctx)

    out = capsys.readouterr().out
    assert "Set Primary" not in out
    assert "ship.set_primary" not in [c["command"] for c in fake_conn.calls]


def test_enter_ship_set_primary_available_with_debug(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory(
        sector={"id": 1, "ships": [{"id": 99, "owner": "derelict", "name": "Derelict"}]},
        debug=True,
    )
    fake_conn.queue("ship.set_primary", {"status": "error", "error": {"message": "not implemented"}})

    inputs = iter(["", "p", "y", "q"])
    monkeypatch.setattr("builtins.input", lambda *_: next(inputs))

    client.enter_ship_menu(ctx)

    out = capsys.readouterr().out
    assert "Set Primary" in out
    assert "ship.set_primary" in [c["command"] for c in fake_conn.calls]


def test_enter_ship_uses_selected_ship_inspection_contract(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    target = {
        "id": 99,
        "name": "Derelict",
        "ship_type": "Scout",
        "owner": "derelict",
    }
    ctx = ctx_factory(sector={"id": 1, "ships": [target]})
    ctx.player_info = {"player": {"ship_id": 10, "username": "test"}}
    fake_conn.queue("ship.inspect", {"status": "ok", "data": {
        "ship": {"ship_id": 99, "name": "Confirmed Derelict", "ship_type": "Scout"},
    }})
    inputs = iter(["", "i", "q"])
    monkeypatch.setattr("builtins.input", lambda *_: next(inputs))

    client.enter_ship_menu(ctx)

    output = capsys.readouterr().out
    assert "Show visible sector data" in output
    assert "Confirmed Derelict" in output
    assert "Server-authoritative ship inspection" in output
    assert fake_conn.calls == [{"command": "ship.inspect", "data": {"ship_id": 99}}]


def test_insurance_placeholder_actions_are_explicit_and_never_claim_success(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory()
    ctx.push("INSURANCE_MAIN")

    for choice in ("l", "b", "c"):
        client.handle_choice(ctx, choice)

    output = capsys.readouterr().out
    assert output.count("Insurance is unavailable") == 3
    assert "do not persist policies or claims" in output
    assert "No request was sent" in output
    assert fake_conn.calls == []
