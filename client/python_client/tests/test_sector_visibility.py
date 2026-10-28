"""Regressions for truthful sector/scan and proximity-limited port display."""

import client

from protocol import normalize_sector


def test_sector_normalization_drops_port_class_and_tracks_missing_fields():
    sector = normalize_sector({
        "sector_id": 12,
        "ports": [{"id": 8, "name": "Quiet Port", "type": 9, "class": 9}],
        "celestial_objects": [],
        "ships_present": [],
    })
    assert sector["port"] == {"present": True, "id": 8, "name": "Quiet Port"}
    assert sector["knowledge"] == {
        "adjacent": False, "ports": True, "planets": True, "ships": True,
    }

    incomplete = normalize_sector({"sector_id": 13})
    assert incomplete["port"] is None
    assert incomplete["knowledge"] == {
        "adjacent": False, "ports": False, "planets": False, "ships": False,
    }


def test_scan_renderer_distinguishes_present_none_and_unreported_without_class():
    present = client._render_scan_card({"status": "ok", "data": {
        "sector_id": 12, "ports": [{"id": 8, "type": 9}],
        "counts": {"ships": 0, "planets": 2},
    }})
    assert "Port: present" in present
    assert "class/details not disclosed" in present
    assert "ships=0" in present and "planets=2" in present
    assert "type" not in present.lower()

    absent = client._render_scan_card({"status": "ok", "data": {
        "sector_id": 12, "ports": [], "counts": {"ships": 0},
    }})
    assert "Port: none" in absent
    assert "planets=unknown" in absent

    missing = client._render_scan_card({"status": "ok", "data": {"sector_id": 12}})
    assert "Port: not reported" in missing
    assert "Adjacent: not reported" in missing
    assert "ships=unknown" in missing and "fighters=unknown" in missing


def test_scan_does_not_retain_legacy_port_class_from_sector_cache(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 12, "port": {"present": True, "id": 8, "class": 9}})
    fake_conn.queue("move.scan", {"status": "ok", "data": {
        "sector_id": 12, "ports": [{"id": 8, "type": 9}],
        "adjacent_sectors": [], "ships_present": [], "celestial_objects": [],
    }})

    client.scan_sector(ctx)

    assert ctx.last_sector_desc["port"] == {"present": True, "id": 8}
    assert "class/details not disclosed" in capsys.readouterr().out


def test_refused_port_info_clears_previous_inventory_and_context(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 12, "port": {"present": True, "id": 8, "name": "Old port"}})
    ctx.state.update({
        "port_inventory": {"id": 7, "commodities": [{"commodity": "ORE", "sells": True}]},
        "is_stardock": True, "is_shipyard_port": True,
        "has_exchange_access": True, "has_tavern_access": True,
    })
    fake_conn.queue("port.info", {"status": "refused", "error": {
        "code": 1403, "message": "Port information is available only in its sector",
    }})

    client.ctx_refresh_port_context(ctx)

    assert "port_inventory" not in ctx.state
    assert not any(ctx.state[key] for key in (
        "is_stardock", "is_shipyard_port", "has_exchange_access", "has_tavern_access",
    ))
    assert "commodities" not in ctx.last_sector_desc["port"]
    assert "available only in its sector" in capsys.readouterr().out


def test_trade_flow_cannot_quote_using_refused_stale_port_data(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory(sector={"id": 12, "port": {"present": True, "id": 7}})
    ctx.state["port_inventory"] = {"id": 7, "commodities": [{
        "commodity": "ORE", "available": 3, "max_quantity": 10,
        "sells": True, "buys": False,
    }]}
    fake_conn.queue("port.info", {"status": "refused", "error": {
        "message": "Port information is available only in its sector",
    }})

    client.ctx_refresh_port_context(ctx)

    client.dock_trade_flow(ctx, "buy")

    assert [call["command"] for call in fake_conn.calls] == ["port.info"]
    assert "available only in its sector" in capsys.readouterr().out
