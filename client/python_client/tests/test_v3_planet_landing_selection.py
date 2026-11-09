"""V3-PLAN-01 client guard for ambiguous authoritative planet listings."""

import client


def test_duplicate_authoritative_planet_ids_do_not_send_ambiguous_landing(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={
        "id": 12,
        "planets": [
            {"id": 77, "name": "First record"},
            {"planet_id": 77, "name": "Conflicting record"},
        ],
    })

    client.land_flow(ctx)

    output = capsys.readouterr().out
    assert "duplicate planet IDs" in output
    assert [call["command"] for call in fake_conn.calls] == []
