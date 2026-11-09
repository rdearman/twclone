"""NAV-02 client acceptance for the canonical refused transwarp envelope."""

import client


def test_transwarp_rejects_noncanonical_refused_response_type():
    """The C server's refused envelope keeps the shared ``error`` type."""
    assert not client._movement_response_type_matches(
        {"status": "refused", "type": "refused"}, "move.transwarp"
    )
    assert client._movement_response_type_matches(
        {"status": "refused", "type": "error"}, "move.transwarp"
    )


def test_transwarp_refused_envelope_refreshes_without_claiming_destination(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    fake_conn.queue("move.transwarp", {
        "status": "refused",
        "type": "error",
        "error": {"message": "Transwarp drive unavailable."},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 17},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 3, "sector": 17}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "44")

    assert client.transwarp_flow(ctx) is False
    assert [call["command"] for call in fake_conn.calls] == [
        "move.transwarp", "move.describe_sector", "player.my_info", "ship.status",
    ]
    assert ctx.last_sector_desc["id"] == 17
    output = capsys.readouterr().out
    assert "Transwarp drive unavailable." in output
    assert "Transwarped to sector 44." not in output


def test_transwarp_nested_refusal_reason_is_presented_without_claiming_destination(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    fake_conn.queue("move.transwarp", {
        "status": "refused",
        "type": "error",
        "error": {"data": {
            "message": "Transwarp interdiction blocked the destination.",
            "hazards": [],
        }},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 17},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 3, "sector": 17}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "44")

    assert client.transwarp_flow(ctx) is False
    output = capsys.readouterr().out
    assert "Transwarp interdiction blocked the destination." in output
    assert "Transwarped to sector 44." not in output


def test_transwarp_explicit_refused_type_reconciles_authoritative_state(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """The server's explicit refused envelope remains a negative outcome."""
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    fake_conn.queue("move.transwarp", {
        "status": "refused",
        "type": "error",
        "error": {"message": "Transwarp destination is interdicted."},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 17},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {"player": {"id": 3, "sector": 17}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "44")

    assert client.transwarp_flow(ctx) is False
    assert [call["command"] for call in fake_conn.calls] == [
        "move.transwarp", "move.describe_sector", "player.my_info", "ship.status",
    ]
    output = capsys.readouterr().out
    assert "Transwarp destination is interdicted." in output
    assert "Transwarped to sector 44." not in output


