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


def test_sector_normalization_does_not_fallback_from_invalid_authoritative_ids():
    malformed_sector = normalize_sector({"sector_id": 0, "id": 99, "name": "Untrusted"})
    assert malformed_sector["id"] is None

    malformed_port = normalize_sector({
        "sector_id": 12,
        "ports": [{"port_id": "8", "id": 7, "name": "Untrusted port"}],
    })
    assert malformed_port["port"] == {"present": True, "name": "Untrusted port"}


def test_sector_normalization_does_not_coerce_malformed_port_presence():
    malformed = normalize_sector({
        "sector_id": 12,
        "port": {"present": "false", "id": 8, "name": "Untrusted port"},
    })

    assert malformed["port"] == {"present": None, "id": 8, "name": "Untrusted port"}


def test_sector_normalization_keeps_only_authoritative_adjacent_ids():
    sector = normalize_sector({
        "sector_id": 12,
        "adjacent_sectors": [13, True, 14.0, "15", {"to_sector": 16}, 0, -1],
    })

    assert sector["adjacent"] == [13]
    assert sector["knowledge"]["adjacent"] is True


def test_sector_info_decoder_failure_keeps_cache_and_reconciles(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A malformed success response cannot replace confirmed visibility state."""
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("sector.info", {
        "status": "ok", "type": "sector.info",
        "data": {"sector_id": 12, "name": "Untrusted"},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"sector_id": 12, "name": "Reconciled"},
    })

    def fail_normalization(_data):
        raise ValueError("malformed visibility payload")

    monkeypatch.setattr(client, "normalize_sector", fail_normalization)
    client.sector_info_flow(ctx)

    assert ctx.last_sector_desc == {"id": 12, "name": "Confirmed"}
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.info", "move.describe_sector"
    ]
    assert "malformed sector information" in capsys.readouterr().out


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


def test_scan_without_confirmed_sector_id_sends_no_request(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"name": "Display-only sector"})

    client.scan_sector(ctx)

    assert fake_conn.calls == []
    assert "no authoritative ID" in capsys.readouterr().out


def test_detailed_scan_without_confirmed_sector_id_sends_no_request(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 0, "name": "Unconfirmed sector"})

    client.detailed_sector_scan_flow(ctx)

    assert fake_conn.calls == []


def test_density_scan_without_confirmed_sector_id_sends_no_request(
    ctx_factory, fake_conn, capsys
):
    """Visibility density must not probe a target without a server-owned ID."""
    ctx = ctx_factory(sector={"id": 12.0, "name": "Schema-mismatched sector"})

    client.sector_density_scan_flow(ctx)

    assert fake_conn.calls == []
    assert "no authoritative ID" in capsys.readouterr().out


def test_scan_does_not_retain_legacy_port_class_from_sector_cache(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 12, "port": {"present": True, "id": 8, "class": 9}})
    fake_conn.queue("move.scan", {"status": "ok", "data": {
        "sector_id": 12, "ports": [{"id": 8, "type": 9}],
        "adjacent_sectors": [], "ships_present": [], "celestial_objects": [],
    }})

    client.scan_sector(ctx)

    assert ctx.last_sector_desc["port"] == {"present": True, "id": 8}
    assert "class/details not disclosed" in capsys.readouterr().out


def test_scan_does_not_coerce_malformed_port_presence_into_cache(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "port": {"present": True, "id": 8}})
    fake_conn.queue("move.scan", {"status": "ok", "data": {
        "sector_id": 12,
        "port": {"present": "false", "id": 8},
        "adjacent_sectors": [], "ships_present": [], "celestial_objects": [],
    }})

    client.scan_sector(ctx)

    # A string is not an authoritative JSON boolean. The scan must not
    # overwrite the confirmed port presence with bool("false") == True.
    assert ctx.last_sector_desc["port"] == {"present": True, "id": 8}
    assert "Port: not reported" in capsys.readouterr().out


def test_scan_does_not_infer_port_presence_from_malformed_port_rows(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "port": {"present": False}})
    fake_conn.queue("move.scan", {"status": "ok", "data": {
        "sector_id": 12,
        "ports": ["untrusted row"],
        "counts": {},
    }})

    client.scan_sector(ctx)

    assert ctx.last_sector_desc["port"] == {"present": False}
    assert "Port: not reported" in capsys.readouterr().out


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


def test_port_info_wrong_response_type_does_not_repopulate_visibility_cache(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "port": {"id": 8, "name": "Current"}})
    ctx.state["port_inventory"] = {"id": 8, "commodities": [{"commodity": "ORE"}]}
    fake_conn.queue("port.info", {
        "status": "ok",
        "type": "sector.info",
        "data": {"port": {"id": 8, "name": "Wrong read", "commodities": []}},
    })

    client.ctx_refresh_port_context(ctx)

    assert "port_inventory" not in ctx.state
    assert "commodities" not in ctx.last_sector_desc["port"]
    assert "unexpected port response type" in capsys.readouterr().out


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


def test_refused_scan_does_not_merge_untrusted_payload(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("move.scan", {"status": "error", "error": {"message": "Remote sector"},
                                   "data": {"sector_id": 999, "name": "Leaked"}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 12, "name": "Known"}})

    client.scan_sector(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert [call["command"] for call in fake_conn.calls] == ["move.scan", "move.describe_sector"]
    assert "Remote sector" in capsys.readouterr().out


def test_scan_with_wrong_success_type_preserves_cache_and_refreshes_sector(
    ctx_factory, fake_conn, capsys
):
    """A success envelope from another operation is not scan evidence."""
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("move.scan", {
        "status": "ok",
        "type": "sector.info",
        "data": {"sector_id": 12, "name": "Untrusted scan"},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Reconciled"},
    })

    client.scan_sector(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["name"] == "Reconciled"
    assert [call["command"] for call in fake_conn.calls] == [
        "move.scan", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "unexpected type" in output
    assert "Untrusted scan" not in output


def test_scan_without_authoritative_sector_id_does_not_merge_payload(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("move.scan", {
        "status": "ok",
        "data": {"name": "Unidentified sector", "counts": {"ships": 9}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Known"},
    })

    client.scan_sector(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert [call["command"] for call in fake_conn.calls] == [
        "move.scan", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "did not include an authoritative sector ID" in output
    assert "Unidentified sector" not in output


def test_refused_density_scan_is_presented_without_changing_cached_sector(
    ctx_factory, fake_conn, capsys
):
    known_sector = {"id": 12, "name": "Known", "port": {"present": True, "id": 8}}
    ctx = ctx_factory(sector=known_sector)
    fake_conn.queue("sector.scan.density", {
        "status": "refused",
        "error": {"message": "Density is unavailable for a remote sector"},
        "data": {"sector_id": 999, "density": 0},
    })
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": known_sector})

    client.sector_density_scan_flow(ctx)

    assert ctx.last_sector_desc["id"] == known_sector["id"]
    assert [call["command"] for call in fake_conn.calls] == ["sector.scan.density", "move.describe_sector"]
    output = capsys.readouterr().out
    assert "Density scan refused" in output
    assert "Density is unavailable for a remote sector" in output
    assert "unexpected response shape" not in output


def test_density_scan_does_not_display_mismatched_single_sector(
    ctx_factory, fake_conn, capsys
):
    """A success-shaped density row for another sector is not usable state."""
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok",
        "data": {"sector_id": 999, "density": 42},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Refreshed"},
    })

    client.sector_density_scan_flow(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["name"] == "Refreshed"
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan.density", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "did not identify the confirmed current sector" in output
    assert "Density: 42" not in output


def test_density_scan_unwraps_authoritative_single_sector_row(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok",
        "data": {"sectors": [{"sector_id": 12, "density": 7}]},
    })

    client.sector_density_scan_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["sector.scan.density"]
    output = capsys.readouterr().out
    assert "Sector: 12" in output
    assert "Density: 7" in output


def test_density_scan_displays_authoritative_object_rows(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok",
        "type": "sector.density.scan",
        "data": {"sectors": [
            {"sector_id": 12, "density": 7},
            {"sector_id": 13, "density": 0},
        ]},
    })

    client.sector_density_scan_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["sector.scan.density"]
    output = capsys.readouterr().out
    assert "12" in output and "7" in output
    assert "13" in output and "0" in output


def test_density_scan_rejects_malformed_authoritative_object_row(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok",
        "type": "sector.density.scan",
        "data": {"sectors": [
            {"sector_id": 12, "density": 7},
            {"sector_id": 13, "density": "unknown"},
        ]},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Refreshed"},
    })

    client.sector_density_scan_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan.density", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "invalid authoritative row" in output
    assert "unknown" not in output


def test_density_scan_does_not_infer_missing_single_row_id(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok", "data": {"density": 7},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Refreshed"},
    })

    client.sector_density_scan_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan.density", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "did not identify the confirmed current sector" in output
    assert "Density: 7" not in output


def test_density_scan_rejects_boolean_compact_sector_id(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok", "data": {"sectors": [True, 7]},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Refreshed"},
    })

    client.sector_density_scan_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan.density", "move.describe_sector",
    ]
    assert "unexpected response shape" in capsys.readouterr().out


def test_density_scan_rejects_non_positive_compact_sector_id(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok", "data": {"sectors": [0, 7]},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Refreshed"},
    })

    client.sector_density_scan_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan.density", "move.describe_sector",
    ]
    assert "unexpected response shape" in capsys.readouterr().out


def test_refused_sector_info_refreshes_confirmed_current_sector_without_merging_payload(
    ctx_factory, fake_conn, capsys
):
    known_sector = {"id": 12, "name": "Known"}
    ctx = ctx_factory(sector=known_sector)
    fake_conn.queue("sector.info", {
        "status": "refused",
        "error": {"message": "Session location is stale"},
        "data": {"sector_id": 999, "name": "Untrusted remote sector"},
    })
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": known_sector})

    client.sector_info_flow(ctx)

    assert ctx.last_sector_desc["id"] == known_sector["id"]
    assert ctx.last_sector_desc["name"] == known_sector["name"]
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.info", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "Session location is stale" in output
    assert "Untrusted remote sector" not in output


def test_malformed_density_scan_refreshes_confirmed_sector(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan.density", {"status": "ok", "data": "not-a-density-response"})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 12, "name": "Refreshed"}})

    client.sector_density_scan_flow(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["name"] == "Refreshed"
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan.density", "move.describe_sector",
    ]
    assert "unexpected response shape" in capsys.readouterr().out


def test_sector_info_without_authoritative_id_refreshes_without_merging_payload(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.info", {"status": "ok", "data": {"name": "Unidentified"}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 12, "name": "Refreshed"}})

    client.sector_info_flow(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["name"] == "Refreshed"
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.info", "move.describe_sector",
    ]
    assert "without an authoritative sector ID" in capsys.readouterr().out


def test_sector_info_mismatched_authoritative_id_refreshes_without_merging_payload(
    ctx_factory, fake_conn, capsys
):
    """A sector.info reply for another sector must not replace current state."""
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("sector.info", {
        "status": "ok",
        "data": {"sector_id": 99, "name": "Remote sector", "ports": []},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Refreshed"},
    })

    client.sector_info_flow(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["name"] == "Refreshed"
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.info", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "sector 99 instead of confirmed sector 12" in output
    assert "Remote sector" not in output


def test_malformed_detailed_scan_refreshes_without_rendering_payload(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan", {"status": "ok", "data": [12, "not-a-sector"]})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 12, "name": "Refreshed"}})

    client.detailed_sector_scan_flow(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["name"] == "Refreshed"
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan", "move.describe_sector",
    ]
    assert "unrecognized sector.scan response" in capsys.readouterr().out


def test_detailed_scan_rejects_success_for_another_sector_and_refreshes(
    ctx_factory, fake_conn, capsys
):
    """A success envelope cannot authorize a scan for a different sector."""
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan", {
        "status": "ok",
        "data": {
            "sector_id": 99,
            "name": "Remote sector",
            "ports": [{"id": 8, "class": 9}],
            "counts": {"ships": 4},
        },
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Refreshed"},
    })

    client.detailed_sector_scan_flow(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["name"] == "Refreshed"
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "did not identify the confirmed current sector" in output
    assert "Remote sector" not in output
    assert "class" not in output.lower()


def test_sector_info_without_authoritative_id_preserves_confirmed_cache(
    ctx_factory, fake_conn, capsys
):
    """A success-shaped response without an ID is not a usable location."""
    known_sector = {"id": 12, "name": "Known"}
    ctx = ctx_factory(sector=known_sector)
    fake_conn.queue("sector.info", {
        "status": "ok",
        "data": {"name": "Unidentified remote sector", "ports": [{"id": 99}]},
    })

    client.sector_info_flow(ctx)

    assert ctx.last_sector_desc["id"] == known_sector["id"]
    assert ctx.last_sector_desc["name"] == known_sector["name"]
    assert "Unidentified remote sector" not in capsys.readouterr().out


def test_authoritative_sector_refresh_rejects_mismatched_id(
    ctx_factory, fake_conn, capsys
):
    """Recovery reads must not replace confirmed state with a remote row."""
    known_sector = {"id": 12, "name": "Known"}
    ctx = ctx_factory(sector=known_sector)
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "data": {"sector_id": 999, "name": "Untrusted remote sector"},
    })

    client.refresh_sector_and_display(ctx)

    assert ctx.last_sector_desc == known_sector
    output = capsys.readouterr().out
    assert "instead of confirmed sector 12" in output
    assert "Untrusted remote sector" not in output


def test_authoritative_sector_refresh_rejects_boolean_id(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"sector_id": True, "name": "Malformed"},
    })

    client.refresh_sector_and_display(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert "no authoritative sector ID" in capsys.readouterr().out


def test_density_scan_rejects_boolean_values_as_compact_ids(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok", "data": [True, 3],
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Refreshed"},
    })

    client.sector_density_scan_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan.density", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "unexpected response shape" in output
    assert "Sector:" not in output


def test_malformed_sector_info_refreshes_confirmed_sector(
    ctx_factory, fake_conn, capsys
):
    """A successful envelope with a non-object payload must not be trusted."""
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.info", {"status": "ok", "data": [12, "not-a-sector"]})
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Refreshed"},
    })

    client.sector_info_flow(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["name"] == "Refreshed"
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.info", "move.describe_sector",
    ]
    assert "unrecognized sector.info response" in capsys.readouterr().out


def test_refused_detailed_scan_does_not_merge_payload_and_refreshes_current_sector(
    ctx_factory, fake_conn, capsys
):
    known_sector = {"id": 12, "name": "Known"}
    ctx = ctx_factory(sector=known_sector)
    fake_conn.queue("sector.scan", {
        "status": "refused",
        "error": {"message": "Detailed scan requires the current sector"},
        "data": {"sector_id": 999, "name": "Leaked remote details"},
    })
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": known_sector})

    client.detailed_sector_scan_flow(ctx)

    assert ctx.last_sector_desc["id"] == known_sector["id"]
    assert ctx.last_sector_desc["name"] == known_sector["name"]
    assert ctx.last_sector_desc["id"] != 999
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "Detailed scan refused" in output
    assert "Detailed scan requires the current sector" in output


def test_scan_refusal_keeps_confirmed_cache_when_authoritative_refresh_is_refused(
    ctx_factory, fake_conn, capsys
):
    """A failed recovery read must not replace confirmed state with its payload."""
    known_sector = {"id": 12, "name": "Known"}
    ctx = ctx_factory(sector=known_sector)
    fake_conn.queue("sector.scan", {
        "status": "refused",
        "error": {"message": "Detailed scan requires the current sector"},
        "data": {"sector_id": 999, "name": "Untrusted scan payload"},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "refused",
        "error": {"message": "Location refresh unavailable"},
        "data": {"sector_id": 999, "name": "Untrusted refresh payload"},
    })

    client.detailed_sector_scan_flow(ctx)

    assert ctx.last_sector_desc == known_sector
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "Untrusted scan payload" not in output
    assert "Untrusted refresh payload" not in output
    assert "Location refresh unavailable" in output


def test_successful_detailed_scan_without_authoritative_id_is_not_rendered(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan", {
        "status": "ok",
        "data": {"name": "Unidentified remote sector", "counts": {"ships": 9}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Known"},
    })

    client.detailed_sector_scan_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "did not identify the confirmed current sector" in output
    assert "Unidentified remote sector" not in output
    assert "Leaked remote details" not in output


def test_scan_with_different_authoritative_sector_id_does_not_replace_confirmed_cache(
    ctx_factory, fake_conn, capsys
):
    known_sector = {"id": 12, "name": "Known"}
    ctx = ctx_factory(sector=known_sector)
    fake_conn.queue("move.scan", {
        "status": "ok",
        "data": {
            "sector_id": 999,
            "name": "Unconfirmed remote sector",
            "ports": [{"id": 77}],
        },
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": known_sector,
    })

    client.scan_sector(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["name"] == "Known"
    assert [call["command"] for call in fake_conn.calls] == [
        "move.scan", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "cached sector state was not changed" in output
    assert "Unconfirmed remote sector" not in output
