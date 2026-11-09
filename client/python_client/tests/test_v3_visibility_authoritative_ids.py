"""V3-VIS-01 client guards for authoritative scan identity."""

import pytest

import client


def test_detailed_scan_rejects_boolean_sector_id_and_refreshes_current_sector(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan", {
        "status": "ok",
        "data": {
            "sector_id": True,
            "name": "Untrusted payload",
            "counts": {"ships": 7},
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
    assert "Untrusted payload" not in output


def test_density_scan_rejects_boolean_sector_id_and_refreshes_current_sector(
    ctx_factory, fake_conn, capsys
):
    """A malformed density row must not become a server-authoritative ID."""
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok",
        "data": {"sectors": [{"sector_id": True, "density": 9}]},
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
    assert "Density: 9" not in output


@pytest.mark.parametrize("invalid_density", [True, -1, 9.0, "9"])
def test_density_scan_rejects_non_integer_density_value(
    ctx_factory, fake_conn, invalid_density, capsys
):
    """A successful envelope with an invalid measurement is not authoritative."""
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok",
        "data": {"sector_id": 12, "density": invalid_density},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Refreshed"},
    })

    client.sector_density_scan_flow(ctx)

    assert ctx.last_sector_desc["name"] == "Refreshed"
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan.density", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "invalid authoritative value" in output
    assert f"Density: {invalid_density}" not in output


def test_scan_does_not_cache_malformed_nested_object_ids(ctx_factory, fake_conn, capsys):
    """A valid scan sector ID does not make nested IDs authoritative."""
    ctx = ctx_factory(sector={"id": 12, "name": "Known", "adjacent": [13]})
    fake_conn.queue("move.scan", {
        "status": "ok",
        "data": {
            "sector_id": 12,
            "name": "Known",
            "adjacent": [13, True, 14.0, "15", -1],
            "ports": [{"id": True, "name": "Untrusted Port"}],
        },
    })

    client.scan_sector(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["adjacent"] == [13]
    assert ctx.last_sector_desc["port"] == {
        "present": True, "name": "Untrusted Port",
    }
    output = capsys.readouterr().out
    assert "Adjacent: 13" in output
    assert "True" not in output
    assert "14.0" not in output


def test_scan_does_not_fallback_from_malformed_port_id_to_legacy_alias(
    ctx_factory, fake_conn, capsys
):
    """An invalid explicit port ID must not redirect to a conflicting alias."""
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("move.scan", {
        "status": "ok",
        "data": {
            "sector_id": 12,
            "ports": [{"id": True, "port_id": 44, "name": "Untrusted port"}],
        },
    })

    client.scan_sector(ctx)

    assert ctx.last_sector_desc["port"] == {
        "present": True, "name": "Untrusted port",
    }
    assert "44" not in capsys.readouterr().out


def test_scan_rejects_invalid_primary_sector_id_instead_of_falling_back_to_alias(
    ctx_factory, fake_conn, capsys
):
    """A malformed sector_id must not be replaced by a conflicting legacy id."""
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("move.scan", {
        "status": "ok",
        "data": {"sector_id": 0, "id": 99, "name": "Untrusted"},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Refreshed"},
    })

    client.scan_sector(ctx)

    assert ctx.last_sector_desc["name"] == "Refreshed"
    assert [call["command"] for call in fake_conn.calls] == [
        "move.scan", "move.describe_sector",
    ]
    assert "did not include an authoritative sector ID" in capsys.readouterr().out


def test_scan_correlates_and_caches_primary_sector_id_when_legacy_alias_conflicts(
    ctx_factory, fake_conn, capsys
):
    """V3-VIS-01 must never let a conflicting legacy ID redirect visibility state."""
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("move.scan", {
        "status": "ok",
        "type": "sector.scan",
        "data": {
            "sector_id": 12,
            "id": 99,
            "name": "Confirmed scan",
            "counts": {"ships": 1},
        },
    })

    client.scan_sector(ctx)

    output = capsys.readouterr().out
    assert "Sector 12" in output
    assert "Sector 99" not in output
    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["name"] == "Confirmed scan"
    assert [call["command"] for call in fake_conn.calls] == ["move.scan"]
