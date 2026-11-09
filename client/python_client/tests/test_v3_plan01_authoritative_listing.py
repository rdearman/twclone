"""V3-PLAN-01 client acceptance for authoritative planet selection."""

import client


def test_landing_prefers_authoritative_planet_id_over_legacy_alias(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(
        sector={
            "id": 17,
            "planets": [{"id": 901, "planet_id": 77, "name": "Terra"}],
        }
    )
    fake_conn.queue("planet.land", {
        "status": "refused",
        "error": {"message": "Landing denied"},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 3, "turns": 40}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 9, "onplanet": 0}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"id": 17, "planets": [{"id": 901, "planet_id": 77}]},
    })

    client.land_flow(ctx)

    assert fake_conn.calls[0] == {
        "command": "planet.land", "data": {"planet_id": 77}
    }
    assert "Landing failed: Landing denied" in capsys.readouterr().out


def test_landing_sends_no_request_when_authoritative_planet_id_is_malformed(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(
        sector={
            "id": 17,
            "planets": [{"id": 901, "planet_id": "77", "name": "Terra"}],
        }
    )

    client.land_flow(ctx)

    assert fake_conn.calls == []
    assert "No listed planet has a usable server ID" in capsys.readouterr().out
