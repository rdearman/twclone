"""Client request contracts for COM-02 sector defence asset workflows."""

import client


def test_fighter_listing_uses_authoritative_empty_request(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 17})
    fake_conn.queue("deploy.fighters.list", {
        "status": "ok",
        "data": {"entries": [{"asset_id": 41, "count": 5, "offense_mode": "DEFEND"}]},
    })

    client.list_deployed_fighters_flow(ctx)

    assert fake_conn.calls == [{"command": "deploy.fighters.list", "data": {}}]
    assert "Asset ID: 41" in capsys.readouterr().out


def test_mine_listing_uses_authoritative_empty_request(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 17})
    fake_conn.queue("deploy.mines.list", {
        "status": "ok",
        "data": {"entries": [{"asset_id": 42, "count": 3, "offense_mode": "TOLL"}]},
    })

    client.list_deployed_mines_flow(ctx)

    assert fake_conn.calls == [{"command": "deploy.mines.list", "data": {}}]
    assert "Asset ID: 42" in capsys.readouterr().out


def test_mine_sweep_targets_confirmed_current_sector(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory(sector={"id": 17})
    fake_conn.queue("combat.sweep_mines", {
        "status": "ok",
        "data": {"success": True, "mines_removed": 2},
    })
    monkeypatch.setattr(client, "refresh_sector_and_display", lambda _ctx: None)

    client.sweep_mines_flow(ctx)

    assert fake_conn.calls == [{
        "command": "combat.sweep_mines",
        "data": {"target_sector_id": 17},
    }]
    assert "2 mine asset(s) removed" in capsys.readouterr().out


def test_mine_sweep_refuses_without_confirmed_sector(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={})

    client.sweep_mines_flow(ctx)

    assert fake_conn.calls == []
    assert "current sector ID is unknown" in capsys.readouterr().out
