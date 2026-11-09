"""NAV-02 client contract: only a confirmed server destination succeeds."""

import client


def test_transwarp_terminal_error_returns_false_after_authoritative_refresh(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    fake_conn.queue("move.transwarp", {
        "status": "error",
        "type": "error",
        "error": {
            "message": "Ship destroyed by sector hazards during transwarp.",
            "data": {
                "sector_id": 44,
                "ship_destroyed": True,
                "hazards": [{"hazard_type": "minefield", "hull_lost": 100}],
            },
        },
        "data": None,
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 1},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {"player": {"id": 3, "sector": 1}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "44")

    assert client.transwarp_flow(ctx) is False
    assert [call["command"] for call in fake_conn.calls] == [
        "move.transwarp", "move.describe_sector", "player.my_info", "ship.status",
    ]
    assert "minefield" in capsys.readouterr().out


def test_transwarp_success_returns_true_only_after_destination_refresh(
    ctx_factory, fake_conn, monkeypatch
):
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    fake_conn.queue("move.transwarp", {
        "status": "ok", "type": "move.transwarp",
        "data": {"sector_id": 44, "status": "transwarp_complete", "hazards": []},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 44},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {"player": {"id": 3, "sector": 44}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "44")

    assert client.transwarp_flow(ctx) is True


def test_transwarp_rejects_noncanonical_success_destination_alias(
    ctx_factory, fake_conn, monkeypatch
):
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    fake_conn.queue("move.transwarp", {
        "status": "ok", "type": "move.transwarp",
        "data": {"current_sector": 44, "status": "transwarp_complete", "hazards": []},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 1},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {"player": {"id": 3, "sector": 1}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "44")

    assert client.transwarp_flow(ctx) is False
    assert [call["command"] for call in fake_conn.calls] == [
        "move.transwarp", "move.describe_sector", "player.my_info", "ship.status",
    ]


def test_transwarp_rejects_success_without_canonical_completion_status(
    ctx_factory, fake_conn, monkeypatch
):
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    fake_conn.queue("move.transwarp", {
        "status": "ok", "type": "move.transwarp",
        "data": {"sector_id": 44, "hazards": []},
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
    assert ctx.last_sector_desc["id"] == 17


def test_transwarp_rejects_success_type_on_negative_envelope(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    fake_conn.queue("move.transwarp", {
        "status": "error", "type": "move.transwarp",
        "error": {"message": "Transwarp refused.", "data": {
            "hazards": [{"hazard_type": "untrusted"}],
        }},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 17},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {"player": {"id": 3}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "44")

    assert client.transwarp_flow(ctx) is False
    output = capsys.readouterr().out
    assert "unexpected type" in output
    assert "untrusted" not in output


def test_transwarp_canonical_error_envelope_renders_server_hazards_and_refreshes(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """Consume the C server's negative movement envelope without inferring success."""
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    fake_conn.queue("move.transwarp", {
        "status": "error", "type": "error",
        "error": {
            "message": "Entry hazard processing failed.",
            "data": {
                "sector_id": 44,
                "hazards": [{"hazard_type": "minefield", "hull_lost": 9}],
            },
        },
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"sector_id": 44, "name": "After"},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 3, "sector": 44}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 9, "hull": 91}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "44")

    assert client.transwarp_flow(ctx) is False
    assert [call["command"] for call in fake_conn.calls] == [
        "move.transwarp", "move.describe_sector", "player.my_info", "ship.status",
    ]
    assert ctx.last_sector_desc["id"] == 44
    output = capsys.readouterr().out
    assert "minefield" in output and "hull lost: 9" in output
    assert "Transwarp response had an unexpected type" not in output
