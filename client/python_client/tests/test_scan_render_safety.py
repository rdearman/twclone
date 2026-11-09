"""Visibility regressions for scan presentation."""

import client


def test_visibility_scalar_renderer_fails_closed_for_schema_mismatches():
    assert client._display_authoritative_value(True) == "true"
    assert client._display_authoritative_value(False) == "false"
    assert client._display_authoritative_value("false") == "unknown"
    assert client._display_authoritative_value(1) == "unknown"


class ExplodingValue:
    def __str__(self):
        raise RuntimeError("unexpected value")


def test_scan_renderer_never_dumps_unrecognized_raw_payload():
    response = {
        "status": "ok",
        "data": {
            "sector_id": 12,
            "name": ExplodingValue(),
            "secret_future_field": "must not be rendered",
        },
    }

    rendered = client._render_scan_card(response)

    assert "unavailable" in rendered
    assert "secret_future_field" not in rendered
    assert "must not be rendered" not in rendered


def test_scan_renderer_fails_closed_for_non_envelope_response():
    rendered = client._render_scan_card(["secret", {"future": "field"}])

    assert "Fast Scan: unavailable" in rendered
    assert "secret" not in rendered
    assert "future" not in rendered


def test_scan_renderer_fails_closed_for_non_object_success_data():
    rendered = client._render_scan_card({
        "status": "ok",
        "data": [12, {"secret_future_field": "must not be rendered"}],
    })

    assert "Fast Scan: unavailable" in rendered
    assert "secret_future_field" not in rendered


def test_scan_renderer_does_not_render_refused_payload():
    rendered = client._render_scan_card({
        "status": "refused",
        "error": {"code": "ERR_REMOTE", "message": "Scan is limited to the current sector."},
        "data": {"sector_id": 999, "name": "Untrusted remote sector", "counts": {"ships": 77}},
    })

    assert "REFUSED" in rendered
    assert "Scan is limited to the current sector." in rendered
    assert "Untrusted remote sector" not in rendered
    assert "999" not in rendered
    assert "77" not in rendered


def test_scan_renderer_rejects_missing_or_malformed_authoritative_sector_id():
    for invalid_id in (None, True, 12.0, "12", 0, -1):
        rendered = client._render_scan_card({
            "status": "ok",
            "data": {"sector_id": invalid_id, "name": "Untrusted sector"},
        })

        assert "unavailable" in rendered
        assert "Untrusted sector" not in rendered


def test_scan_renderer_does_not_coerce_visibility_scalars():
    rendered = client._render_scan_card({
        "status": "ok",
        "data": {
            "sector_id": 12,
            "name": "Known",
            "security": {
                "fedspace": "false",
                "safe_zone": 1,
                "combat_locked": None,
            },
            "port": {"present": "false"},
        },
    })

    assert "fedspace=true" not in rendered
    assert "fedspace=unknown" in rendered
    assert "safe_zone=unknown" in rendered
    assert "combat_locked=unknown" in rendered
    assert "Port: not reported" in rendered
    assert "Known" in rendered


def test_scan_renderer_rejects_negative_entity_counts():
    rendered = client._render_scan_card({
        "status": "ok",
        "data": {
            "sector_id": 12,
            "counts": {"ships": -1, "planets": 2, "mines": -3, "fighters": 0},
        },
    })

    assert "ships=unknown" in rendered
    assert "planets=2" in rendered
    assert "mines=unknown" in rendered
    assert "fighters=0" in rendered


def test_scan_flow_never_dumps_raw_payload_when_presenter_fails(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("move.scan", {
        "status": "ok",
        "data": {"sector_id": 12, "secret_future_field": "must not be rendered"},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Refreshed"},
    })
    monkeypatch.setattr(client, "_render_scan_card", lambda _response: (_ for _ in ()).throw(RuntimeError("presenter failure")))

    client.scan_sector(ctx)

    output = capsys.readouterr().out
    assert "Fast Scan: unavailable" in output
    assert "secret_future_field" not in output
    assert "must not be rendered" not in output
    assert [call["command"] for call in fake_conn.calls] == [
        "move.scan", "move.describe_sector",
    ]
    assert ctx.last_sector_desc["name"] == "Refreshed"


def test_unrenderable_scan_does_not_merge_payload_and_refreshes_current_sector(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("move.scan", {
        "status": "ok",
        "data": {
            "sector_id": 12,
            "name": ExplodingValue(),
            "adjacent": [99],
        },
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "data": {"id": 12, "name": "Refreshed"},
    })

    client.scan_sector(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "move.scan", "move.describe_sector",
    ]
    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["name"] == "Refreshed"
    assert 99 not in (ctx.last_sector_desc.get("adjacent") or [])
    assert "99" not in capsys.readouterr().out


def test_scan_refusal_refresh_does_not_dump_untrusted_refresh_envelope(
    ctx_factory, fake_conn, capsys
):
    """A failed reconciliation must expose only its safe error message."""
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("move.scan", {
        "status": "refused",
        "error": {"message": "Detailed scan requires the current sector."},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "refused",
        "error": {
            "message": "Sector refresh refused.",
            "secret_future_field": "must not be rendered",
            "data": {"remote_sector_name": "Untrusted remote sector"},
        },
    })

    client.scan_sector(ctx)

    output = capsys.readouterr().out
    assert "Sector refresh refused." in output
    assert "secret_future_field" not in output
    assert "must not be rendered" not in output
    assert "Untrusted remote sector" not in output
    assert ctx.last_sector_desc == {"id": 12, "name": "Known"}
