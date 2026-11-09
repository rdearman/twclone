"""V3-PLAN-01 acceptance for supported planet ID aliases."""

import client


def test_landing_uses_valid_planet_id_when_legacy_id_alias_is_malformed(
    ctx_factory, fake_conn, monkeypatch
):
    """A malformed optional alias must not hide the authoritative planet ID."""
    ctx = ctx_factory(
        sector={
            "id": 17,
            "planets": [{"id": True, "planet_id": 77, "name": "Terra"}],
        }
    )
    fake_conn.queue("planet.land", {"status": "error", "error": {"message": "refused"}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 3}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 17}})
    monkeypatch.setattr("builtins.input", lambda *_: "")

    client.land_flow(ctx)

    assert fake_conn.calls[0] == {"command": "planet.land", "data": {"planet_id": 77}}


def test_landing_does_not_use_conflicting_legacy_id_over_authoritative_planet_id(
    ctx_factory, fake_conn, monkeypatch
):
    """A contradictory alias must not redirect a landing request."""
    ctx = ctx_factory(
        sector={
            "id": 17,
            "planets": [{"id": 11, "planet_id": 77, "name": "Contradictory"}],
        }
    )
    monkeypatch.setattr("builtins.input", lambda *_: "")

    client.land_flow(ctx)

    assert fake_conn.calls[0] == {
        "command": "planet.land",
        "data": {"planet_id": 77},
    }
