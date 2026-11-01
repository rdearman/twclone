"""Player-facing news/readout flows shared with the v2 gameplay backlog."""

import client


def test_news_feed_prints_article_title_time_category_and_body(ctx_factory, capsys):
    ctx = ctx_factory()
    ctx.state["last_rpc"] = {
        "status": "ok",
        "data": {
            "articles": [{
                "id": 5,
                "headline": "Frontier Trade",
                "timestamp": "2026-09-27T12:00:00Z",
                "category": "market",
                "body": "Ports report increased demand.",
            }]
        },
    }

    client.pretty_print_news_feed(ctx)

    output = capsys.readouterr().out
    assert "Frontier Trade" in output
    assert "2026-09-27T12:00:00Z" in output
    assert "Category: market" in output
    assert "Ports report increased demand." in output


def test_news_feed_empty_response_is_readable(ctx_factory, capsys):
    ctx = ctx_factory()
    ctx.state["last_rpc"] = {"status": "ok", "data": {"articles": []}}

    client.pretty_print_news_feed(ctx)

    assert "(No news available)" in capsys.readouterr().out


def test_shipyard_menu_submits_server_supported_repair_request(ctx_factory, fake_conn, menus):
    ctx = ctx_factory()
    options = {item["key"]: item for item in menus["SHIPYARD_MAIN"]["options"]}
    repair = options["r"]["action"]
    fake_conn.queue("ship.repair", {
        "status": "ok",
        "type": "ship.repair",
        "data": {"repaired": True, "cost": 120, "hull": 100},
    })
    fake_conn.queue("player.my_info", {"status": "ok", "type": "player.info", "data": {"credits": 880}})
    fake_conn.queue("ship.status", {"status": "ok", "type": "ship.status", "data": {"ship": {"id": 7, "hull": 100}}})

    client.dispatch_action(ctx, repair)

    assert fake_conn.calls[:1] == [{"command": "ship.repair", "data": {}}]
    assert [call["command"] for call in fake_conn.calls] == ["ship.repair", "player.my_info", "ship.status"]


def test_ship_repair_rejects_untyped_success_and_refreshes_ship(ctx_factory, fake_conn, menus, capsys):
    ctx = ctx_factory()
    action = {"pycall": "ship_repair_flow"}
    fake_conn.queue("ship.repair", {"status": "ok", "data": {"repaired": True, "cost": 0, "hull": 100}})
    fake_conn.queue("ship.status", {"status": "ok", "type": "ship.status", "data": {"ship": {"id": 7, "hull": 90}}})

    client.dispatch_action(ctx, action)

    assert [call["command"] for call in fake_conn.calls] == ["ship.repair", "ship.status"]
    assert "unexpected response type" in capsys.readouterr().out


def test_ship_repair_refusal_refreshes_authoritative_credits_and_hull(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("ship.repair", {
        "status": "refused", "error": {"message": "Insufficient credits"}
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {"credits": 80}
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 7, "hull": 62}}
    })

    client.dispatch_action(ctx, {"pycall": "ship_repair_flow"})

    assert [call["command"] for call in fake_conn.calls] == [
        "ship.repair", "player.my_info", "ship.status"
    ]
    assert "Insufficient credits" in capsys.readouterr().out


def test_shipyard_sale_is_visible_but_does_not_call_unimplemented_server_handler(
    ctx_factory, fake_conn, menus, capsys
):
    ctx = ctx_factory()
    options = {item["key"]: item for item in menus["SHIPYARD_MAIN"]["options"]}

    client.dispatch_action(ctx, options["s"]["action"])

    assert fake_conn.calls == []
    assert "Shipyard sale unavailable" in capsys.readouterr().out
