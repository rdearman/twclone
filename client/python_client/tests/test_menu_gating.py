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
    "move.autopilot.control",
    "ship.set_primary",
    "move.intercept",
    "map.render",
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

    main_keys = {o["key"] for o in _visible_options(menus["MAIN"], ctx)}
    assert "y" not in main_keys  # Testing Menu
    assert "b" not in main_keys  # Bulk Execute

    dock_keys = {o["key"] for o in _visible_options(menus["DOCK"], ctx)}
    assert "t" not in dock_keys  # raw JSON Trade

    move_keys = {o["key"] for o in _visible_options(menus["MOVE"], ctx)}
    assert "k" not in move_keys  # Autopilot Controls (Y/N/E)

    computer_keys = {o["key"] for o in _visible_options(menus["COMPUTER"], ctx)}
    assert "g" not in computer_keys  # Get game clock

    exchange_keys = {o["key"] for o in _visible_options(menus["EXCHANGE_MAIN"], ctx)}
    assert "l" not in exchange_keys  # List Stocks (server subcommand not implemented)
    assert "p" not in exchange_keys  # View Portfolio (server subcommand not implemented)
    assert "d" not in exchange_keys  # Declare Dividend (no test evidence)


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


def test_no_normal_mode_rpc_action_uses_known_absent_command(ctx_factory, menus):
    ctx = ctx_factory(debug=False)

    # Only walk what's actually reachable from MAIN in normal mode; menus
    # gated behind --debug (e.g. TESTING, BULK) are excluded by the
    # show_if_ctx check inside _collect_rpc_commands before recursion.
    reachable_commands = _collect_rpc_commands(menus, "MAIN", ctx)

    absent_in_use = reachable_commands & KNOWN_ABSENT_COMMANDS
    assert absent_in_use == set(), f"Normal-mode menus reference absent commands: {absent_in_use}"


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
