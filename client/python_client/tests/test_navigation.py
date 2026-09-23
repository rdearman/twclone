"""
Tests for main-menu navigation hierarchy, submenu grouping, and hotkey preservation.
"""
import pytest
import client


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


def test_normal_main_contains_intended_top_level_hotkeys(ctx_factory, menus):
    ctx = ctx_factory(debug=False)
    ctx.state["in_corporation"] = False
    ctx.state["has_exchange_access"] = True  # to make SERVICES visible
    client.compute_flags(ctx)

    visible_keys = {o["key"] for o in _visible_options(menus["MAIN"], ctx)}
    
    # Intended core keys
    expected_normal_keys = {"m", "d", "s", "f", "c", "g", "n", "o", "u", "h", "q"}
    for key in expected_normal_keys:
        assert key in visible_keys, f"Key {key!r} should be present in normal MAIN menu"


def test_debug_entries_absent_in_normal_present_in_debug(ctx_factory, menus):
    ctx_normal = ctx_factory(debug=False)
    client.compute_flags(ctx_normal)
    normal_keys = {o["key"] for o in _visible_options(menus["MAIN"], ctx_normal)}
    assert "y" not in normal_keys
    assert "b" not in normal_keys

    ctx_debug = ctx_factory(debug=True)
    client.compute_flags(ctx_debug)
    debug_keys = {o["key"] for o in _visible_options(menus["MAIN"], ctx_debug)}
    assert "y" in debug_keys
    assert "b" in debug_keys


test_hotkey_meanings_preserved = [
    ("c", "COMPUTER", "Ship's Computer"),
    ("g", "COMMS", "Comms"),
    ("o", "CORPORATION_MAIN", "Corporation"),
    ("f", "DEPLOYMENT_MAIN", "Operations & Deployment"),
]


@pytest.mark.parametrize("key,expected_submenu,label_part", test_hotkey_meanings_preserved)
def test_hotkey_meanings_preserved(key, expected_submenu, label_part, ctx_factory, menus):
    ctx = ctx_factory(debug=False)
    client.compute_flags(ctx)
    opts = {o["key"]: o for o in _visible_options(menus["MAIN"], ctx)}
    assert key in opts
    opt = opts[key]
    assert opt.get("action", {}).get("submenu") == expected_submenu
    assert label_part in opt.get("label", "")


def test_corporation_menu_accessible_when_not_in_corporation(ctx_factory, menus):
    ctx = ctx_factory(debug=False)
    ctx.player_info = {}
    ctx.state["in_corporation"] = False
    client.compute_flags(ctx)

    main_opts = {o["key"]: o for o in _visible_options(menus["MAIN"], ctx)}
    assert "o" in main_opts, "Corporation menu must be accessible to non-corp players"

    corp_opts = {o["key"]: o for o in _visible_options(menus["CORPORATION_MAIN"], ctx)}
    assert "c" in corp_opts  # Create Corp
    assert "j" in corp_opts  # Join Corp
    assert "l" in corp_opts  # List All Corporations


def test_services_menu_visibility_when_service_flags_change(ctx_factory, menus):
    # Absent when all service flags are False
    ctx_none = ctx_factory(debug=False)
    ctx_none.state["has_exchange_access"] = False
    ctx_none.state["has_insurance_access"] = False
    ctx_none.state["has_tavern_access"] = False
    ctx_none.state["is_shipyard_port"] = False
    ctx_none.state["has_shipyard_access"] = False
    client.compute_flags(ctx_none)

    none_keys = {o["key"] for o in _visible_options(menus["MAIN"], ctx_none)}
    assert "s" not in none_keys

    # Present when at least one service flag is True
    for flag in ["has_exchange_access", "has_insurance_access", "has_tavern_access", "is_shipyard_port", "has_shipyard_access"]:
        ctx = ctx_factory(debug=False)
        ctx.state[flag] = True
        client.compute_flags(ctx)

        keys = {o["key"] for o in _visible_options(menus["MAIN"], ctx)}
        assert "s" in keys, f"SERVICES menu should appear when {flag} is True"


def test_service_entries_preserve_original_visibility_conditions(ctx_factory, menus):
    ctx = ctx_factory(debug=False)
    ctx.state["has_exchange_access"] = True
    ctx.state["has_tavern_access"] = False
    ctx.state["has_insurance_access"] = False
    ctx.state["is_shipyard_port"] = False
    ctx.state["has_shipyard_access"] = False
    client.compute_flags(ctx)

    service_opts = {o["key"]: o for o in _visible_options(menus["SERVICES"], ctx)}
    assert "x" in service_opts
    assert "t" not in service_opts
    assert "s" not in service_opts
    assert "i" not in service_opts


def test_tactical_actions_reachable_through_deployment_main(ctx_factory, menus):
    ctx = ctx_factory(debug=False)
    ctx.state["sector_info"] = {
        "beacon": "",  # can_set_beacon
        "ships": [
            {"id": 10, "owner": "other", "name": "Derelict"}
        ]
    }
    client.compute_flags(ctx)

    deploy_opts = {o["key"]: o for o in _visible_options(menus["DEPLOYMENT_MAIN"], ctx)}
    assert "f" in deploy_opts  # Deploy Fighters
    assert "m" in deploy_opts  # Deploy Mines
    assert "r" in deploy_opts  # Release Beacon
    assert "w" in deploy_opts  # Tow
    assert "e" in deploy_opts  # Enter Ship


def test_moved_tactical_actions_preserve_handlers_and_rpcs(menus):
    opts = {o["key"]: o for o in _iter_options(menus["DEPLOYMENT_MAIN"])}
    assert opts["r"]["action"] == {"pycall": "set_beacon_flow"}
    assert opts["w"]["action"] == {"pycall": "tow_flow"}
    assert opts["e"]["action"] == {"pycall": "enter_ship_menu"}


def test_submenus_have_functioning_back_action(menus):
    for menu_id, menu in menus.items():
        if menu_id == "MAIN":
            continue
        options = list(_iter_options(menu))
        back_opts = [o for o in options if o.get("action", {}).get("back") is True]
        assert len(back_opts) > 0, f"Submenu {menu_id} must contain a Back action"
