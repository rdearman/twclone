"""V3-VIS-01 response-type correlation and authoritative refresh coverage."""

import client


def test_scan_wrong_explicit_type_is_not_rendered_and_refreshes(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("move.scan", {
        "status": "ok",
        "type": "sector.info",
        "data": {"sector_id": 12, "name": "Wrong response", "counts": {"ships": 9}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "type": "sector.info",
        "data": {"id": 12, "name": "Authoritative refresh"},
    })

    client.scan_sector(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["name"] == "Authoritative refresh"
    assert "Wrong response" not in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "move.scan", "move.describe_sector",
    ]


def test_scan_missing_response_type_is_not_rendered_and_refreshes(
    ctx_factory, fake_conn, capsys
):
    """A scan envelope without the published type is not a response variant."""
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("move.scan", {
        "status": "ok",
        "type": None,
        "data": {"sector_id": 12, "name": "Untrusted untyped sector"},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "type": "sector.info",
        "data": {"id": 12, "name": "Authoritative refresh"},
    })

    client.scan_sector(ctx)

    output = capsys.readouterr().out
    assert "Untrusted untyped sector" not in output
    assert "unexpected type" in output
    assert ctx.last_sector_desc["name"] == "Authoritative refresh"
    assert [call["command"] for call in fake_conn.calls] == [
        "move.scan", "move.describe_sector",
    ]


def test_density_wrong_explicit_type_is_not_rendered_and_refreshes(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok",
        "type": "sector.info",
        "data": {"sector_id": 12, "density": 42},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "type": "sector.info",
        "data": {"id": 12, "name": "Authoritative refresh"},
    })

    client.sector_density_scan_flow(ctx)

    output = capsys.readouterr().out
    assert "Density: 42" not in output
    assert "unexpected type" in output
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan.density", "move.describe_sector",
    ]


def test_density_non_string_explicit_type_is_not_rendered_and_refreshes(
    ctx_factory, fake_conn, capsys
):
    """JSON type mismatches must fail closed instead of being string-coerced."""
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok",
        "type": 1,
        "data": {"sector_id": 12, "density": 42},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "type": "sector.info",
        "data": {"id": 12, "name": "Authoritative refresh"},
    })

    client.sector_density_scan_flow(ctx)

    output = capsys.readouterr().out
    assert "Density: 42" not in output
    assert "unexpected type" in output
    assert ctx.last_sector_desc["name"] == "Authoritative refresh"
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan.density", "move.describe_sector",
    ]


def test_density_accepts_authoritative_server_response_type_without_refresh(
    ctx_factory, fake_conn, capsys
):
    """The server's schema response type is authoritative for density scans."""
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok",
        "type": "sector.density.scan",
        "data": {"sector_id": 12, "density": 42},
    })

    client.sector_density_scan_flow(ctx)

    output = capsys.readouterr().out
    assert "Sector: 12" in output
    assert "Density: 42" in output
    assert [call["command"] for call in fake_conn.calls] == ["sector.scan.density"]


def test_fast_scan_accepts_authoritative_sector_scan_response_type_without_refresh(
    ctx_factory, fake_conn, capsys
):
    """move.scan uses the C server's shared sector.scan response schema."""
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("move.scan", {
        "status": "ok",
        "type": "sector.scan",
        "data": {"sector_id": 12, "counts": {"ships": 2}},
    })

    client.scan_sector(ctx)

    output = capsys.readouterr().out
    assert "Sector 12" in output
    assert "ships=2" in output
    assert [call["command"] for call in fake_conn.calls] == ["move.scan"]


def test_density_compact_payload_rejects_one_malformed_pair_and_refreshes(
    ctx_factory, fake_conn, capsys
):
    """Do not render a partially trusted compact density response."""
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok",
        "type": "sector.scan.density",
        "data": [12, 3, 13.0, 7],
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "type": "sector.info",
        "data": {"id": 12, "name": "Authoritative refresh"},
    })

    client.sector_density_scan_flow(ctx)

    output = capsys.readouterr().out
    assert "Density: 3" not in output
    assert "unexpected response shape" in output
    assert ctx.last_sector_desc["name"] == "Authoritative refresh"
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan.density", "move.describe_sector",
    ]
