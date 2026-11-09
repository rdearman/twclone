"""NAV-02 regression for refused transwarp envelopes that echo a destination."""

import client


def test_transwarp_refusal_cannot_claim_destination_from_refused_data(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A refusal payload remains negative even if it echoes a sector ID."""
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    fake_conn.queue("move.transwarp", {
        "status": "refused",
        "type": "error",
        "error": {"message": "Transwarp destination is blocked."},
        "data": {"sector_id": 44, "status": "transwarp_complete"},
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
    assert "Transwarp destination is blocked." in output
    assert "Transwarped to sector 44." not in output
