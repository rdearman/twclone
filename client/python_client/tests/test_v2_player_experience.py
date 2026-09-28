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

    client.dispatch_action(ctx, repair)

    assert fake_conn.calls[-1] == {"command": "ship.repair", "data": {}}
