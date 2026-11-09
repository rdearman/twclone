"""V3-PLAN-01 client contract for authoritative planet follow-up reads."""

import client


def test_planet_info_requires_authoritative_response_type(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.info", {
        "status": "ok",
        "type": "player.info",
        "data": {"planet_id": 77, "name": "wrong response source"},
    })

    assert client._planet_info(ctx, 77) is None
    assert "unexpected response type" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == ["planet.info"]


def test_planet_info_accepts_matching_authoritative_response(ctx_factory, fake_conn):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.info", {
        "status": "ok",
        "type": "planet.info",
        "data": {"planet_id": 77, "name": "Terra"},
    })

    assert client._planet_info(ctx, 77) == {"planet_id": 77, "name": "Terra"}
