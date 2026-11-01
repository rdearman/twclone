"""Focused tests for the quoted dock trading workflow."""

import uuid

import client
import pytest


PORT_RESPONSE = {
    "status": "ok",
    "data": {"port": {"id": 7, "name": "New Dawn", "commodities": [
        {"code": "ORE", "quantity": 12, "max_quantity": 100, "price": 40,
         "sells": True, "buys": True},
        {"code": "ORG", "quantity": 100, "max_quantity": 100, "price": 25,
         "sells": True, "buys": False},
    ]}},
}


def test_port_info_normalizes_stock_capacity_and_availability():
    normalized = client.normalize_port_info(PORT_RESPONSE)
    assert normalized["name"] == "New Dawn"
    assert normalized["commodities"] == [
        {"commodity": "ORE", "available": 12, "max_quantity": 100,
         "base_price": 40, "sells": True, "buys": True},
        {"commodity": "ORG", "available": 100, "max_quantity": 100,
         "base_price": 25, "sells": True, "buys": False},
    ]


def test_port_info_rejects_explicit_header_sector_mismatch(ctx_factory, fake_conn):
    """A delayed port refresh must not make another sector actionable."""
    ctx = ctx_factory({"sector_id": 17, "port": {"id": 7}})
    fake_conn.queue("port.info", {
        "status": "ok", "type": "trade.port_info",
        "data": {"port": {"id": 7, "sector": 18, "commodities": []}},
    })

    response = client._refresh_port_info(ctx, {"port_id": 7, "sector_id": 17})

    assert response["status"] == "ok"
    assert "port_inventory" not in ctx.state
    assert "different sector" in ctx.state["port_info_error"]


def test_port_info_uses_authoritative_capacity_field():
    """The released trade.port_info contract names capacity, not max_quantity."""
    response = {
        "status": "ok",
        "type": "trade.port_info",
        "data": {"port": {"id": 7, "commodities": [{
            "code": "ORE", "quantity": 12, "capacity": 100,
            "buy_price": 40, "sell_price": 35, "illegal": False,
        }]}},
    }

    normalized = client.normalize_port_info(response)

    assert normalized["commodities"] == [{
        "commodity": "ORE", "available": 12, "max_quantity": 100,
        "base_price": 40, "sells": None, "buys": None,
    }]


def test_trade_rows_keep_commodities_when_direction_is_not_disclosed(ctx_factory):
    """An omitted fixed mode remains server-authorized unknown, not malformed."""
    ctx = ctx_factory({"id": 17, "port": {"id": 7}})
    ctx.state["port_inventory"] = client.normalize_port_info({
        "status": "ok", "type": "trade.port_info",
        "data": {"port": {"id": 7, "commodities": [{
            "code": "ORE", "quantity": 3, "capacity": 10,
        }]}},
    })

    assert [row["commodity"] for row in client._trade_rows(ctx, "buy")] == ["ORE"]
    assert [row["commodity"] for row in client._trade_rows(ctx, "sell")] == ["ORE"]


def test_trade_rows_keep_requested_direction_when_filtering_capabilities(ctx_factory):
    """The port row filter must not replace the requested buy/sell direction."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7}})
    ctx.state["port_inventory"] = {"id": 7, "commodities": [
        {"commodity": "ORE", "sells": True, "buys": False},
        {"commodity": "ORG", "sells": False, "buys": True},
    ]}

    assert [row["commodity"] for row in client._trade_rows(ctx, "buy")] == ["ORE"]
    assert [row["commodity"] for row in client._trade_rows(ctx, "sell")] == ["ORG"]


def test_trade_rows_reject_explicit_null_direction_metadata(ctx_factory):
    """A stale cache with explicit null mode metadata is not actionable."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7}})
    ctx.state["port_inventory"] = {"id": 7, "commodities": [
        {"commodity": "ORE", "sells": None, "buys": True},
        {"commodity": "ORG", "sells": True, "buys": None},
        {"commodity": "EQU", "sells": True, "buys": True},
    ]}

    assert [row["commodity"] for row in client._trade_rows(ctx, "buy")] == ["ORG", "EQU"]
    assert [row["commodity"] for row in client._trade_rows(ctx, "sell")] == ["ORE", "EQU"]


def test_port_info_rejects_explicit_null_fixed_mode_like_godot():
    """Explicit null is malformed mode metadata, not an omitted mode."""
    response = {
        "status": "ok",
        "type": "trade.port_info",
        "data": {"port": {"id": 7, "commodities": [
            {"code": "ORE", "quantity": 3, "capacity": 10,
             "sells": None, "buys": True},
            {"code": "ORG", "quantity": 3, "capacity": 10},
        ]}},
    }

    normalized = client.normalize_port_info(response)

    assert [row["commodity"] for row in normalized["commodities"]] == ["ORG"]
    ctx = type("TradeContext", (), {"state": {"port_inventory": normalized}})()
    assert [row["commodity"] for row in client._trade_rows(ctx, "buy")] == ["ORG"]
    assert [row["commodity"] for row in client._trade_rows(ctx, "sell")] == ["ORG"]


def test_trade_refresh_omits_malformed_optional_sector_identity(ctx_factory):
    """Trade reconciliation must not send a stale/non-integer sector ID."""
    ctx = ctx_factory({"id": "not-a-sector", "port": {"id": 7}})

    assert client._trade_port_refresh_request(ctx, 7) == {"port_id": 7}

    ctx.last_sector_desc["id"] = True
    assert client._trade_port_refresh_request(ctx, 7) == {"port_id": 7}

    ctx.last_sector_desc["id"] = 9
    assert client._trade_port_refresh_request(ctx, 7) == {
        "port_id": 7, "sector_id": 9,
    }


@pytest.mark.parametrize("port_id", [None, True, 0, -3, 7.0, "7"])
def test_trade_refresh_does_not_construct_request_without_authoritative_port_id(
    ctx_factory, port_id
):
    """An uncertain trade must not refresh an inferred or malformed port."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7}})

    assert client._trade_port_refresh_request(ctx, port_id) == {}


def test_trade_contract_requires_explicit_canonical_response_types():
    assert client._trade_quote_type_matches({"status": "ok", "data": {}}) is False
    assert client._trade_quote_type_matches(
        {"status": "ok", "type": "trade.quote", "data": {}}
    ) is True
    assert client._trade_receipt_type_matches(
        {"status": "ok", "data": {}}, "buy"
    ) is False
    assert client._trade_receipt_type_matches(
        {"status": "ok", "type": "trade.sell_receipt_v1", "data": {}}, "sell"
    ) is True


@pytest.mark.parametrize(
    ("presenter", "response_type", "player_id", "message"),
    [
        ("pretty_print_trade_receipt", None, 9, "Unexpected trade.buy receipt response type"),
        ("pretty_print_sell_receipt", "trade.sell_receipt_v1", 99, "did not identify the authenticated player"),
    ],
)
def test_receipt_presenters_fail_closed_on_untrusted_success(
    ctx_factory, presenter, response_type, player_id, message, capsys
):
    """Receipt menus must not display success without the canonical identity."""
    ctx = ctx_factory({"id": 7})
    ctx.state["player_id"] = 9
    response = {
        "status": "ok",
        "data": {
            "port_id": 7,
            "sector_id": 9,
            "player_id": player_id,
            "lines": [{"commodity": "ORE", "quantity": 1}],
        },
    }
    if response_type is not None:
        response["type"] = response_type
    ctx.state["last_rpc"] = response

    getattr(client, presenter)(ctx)

    output = capsys.readouterr().out
    assert message in output
    assert "Trade Receipt" not in output


@pytest.mark.parametrize("player_id, expected, valid", [
    (14, 14, True),
    (99, 14, False),
    (None, 14, False),
    (True, 14, False),
])
def test_trade_receipt_requires_authenticated_player_identity(player_id, expected, valid):
    response = {"status": "ok", "type": "trade.buy_receipt_v1",
                "data": {"player_id": player_id}}
    assert client._trade_receipt_player_matches(response, expected) is valid


@pytest.mark.parametrize("data", [
    {"port_id": 7, "sector_id": 9, "lines": []},
    {"port_id": 0, "sector_id": 9, "lines": [{"commodity": "ORE", "quantity": 1}]},
    {"port_id": 7, "sector_id": "9", "lines": [{"commodity": "ORE", "quantity": 1}]},
    {"port_id": 7, "sector_id": 9, "lines": [{"commodity": " ORE ", "quantity": 1}]},
    {"port_id": 7, "sector_id": 9, "lines": [{"commodity": "ORE", "quantity": 0}]},
])
def test_trade_receipt_presenter_requires_authoritative_location_and_lines(data):
    response = {"status": "ok", "type": "trade.buy_receipt_v1", "data": data}
    assert client._trade_receipt_display_data_is_actionable(response) is False


def test_trade_receipt_presenter_accepts_typed_authoritative_lines():
    response = {
        "status": "ok", "type": "trade.sell_receipt_v1",
        "data": {"port_id": 7, "sector_id": 9,
                 "lines": [{"commodity": "ORE", "quantity": 2}]},
    }
    assert client._trade_receipt_display_data_is_actionable(response) is True


def test_quote_display_requires_canonical_response_type(ctx_factory, capsys):
    """The display helper must not turn another response into a trade quote."""
    ctx = ctx_factory({"id": 7})
    ctx.state["last_rpc"] = {
        "status": "ok",
        "data": {"port_id": 7, "commodity": "ORE", "quantity": 1},
    }

    client.pretty_print_trade_quote(ctx)

    output = capsys.readouterr().out
    assert "Unexpected trade.quote response type" in output
    assert "Trade Quote" not in output


def test_quote_display_does_not_render_refused_envelope_with_echoed_data(ctx_factory, capsys):
    """A refused quote never becomes an actionable trade surface."""
    ctx = ctx_factory({"id": 7})
    ctx.state["last_rpc"] = {
        "status": "refused",
        "type": "trade.quote",
        "error": {"message": "Commodity direction is not permitted"},
        "data": {"port_id": 7, "commodity": "ORE", "quantity": 1,
                 "buy_price": 40, "total_buy_price": 40},
    }

    client.pretty_print_trade_quote(ctx)

    output = capsys.readouterr().out
    assert "Trade quote refused" in output
    assert "Commodity direction is not permitted" in output
    assert "Trade Quote" not in output


@pytest.mark.parametrize("malformed_data", [
    {"port_id": True, "commodity": "ORE", "quantity": 1},
    {"port_id": 7, "commodity": None, "quantity": 1},
    {"port_id": 7, "commodity": " ORE ", "quantity": 1},
    {"port_id": 7, "commodity": "ORE", "quantity": 0},
])
def test_quote_display_rejects_malformed_authoritative_identity(
    ctx_factory, malformed_data, capsys
):
    """A typed quote with invalid identity must not become a trade surface."""
    ctx = ctx_factory({"id": 7})
    ctx.state["last_rpc"] = {
        "status": "ok", "type": "trade.quote", "data": malformed_data,
    }

    client.pretty_print_trade_quote(ctx)

    output = capsys.readouterr().out
    assert "Invalid trade.quote response data" in output
    assert "Trade Quote" not in output


@pytest.mark.parametrize("field", ["buy_price", "sell_price", "total_buy_price", "total_sell_price"])
def test_trade_quote_validator_rejects_malformed_authoritative_price(field):
    """The playable trade path must not confirm a quote with invalid prices."""
    data = {"port_id": 7, "commodity": "ORE", "quantity": 1,
            "buy_price": 40, "sell_price": 35,
            "total_buy_price": 40, "total_sell_price": 35}
    data[field] = True

    assert client._trade_quote_display_data_is_actionable(data) is False


def test_port_info_does_not_derive_fixed_direction_from_stock():
    response = {"status": "ok", "data": {"port": {"id": 7, "commodities": [
        {"code": "ORE", "quantity": 0, "max_quantity": 100, "price": 40},
        {"code": "ORG", "quantity": 100, "max_quantity": 100, "price": 25},
    ]}}}

    normalized = client.normalize_port_info(response)
    assert normalized["commodities"][0]["sells"] is None
    assert normalized["commodities"][0]["buys"] is None
    assert len(client._trade_rows(type("Ctx", (), {"state": {"port_inventory": normalized}})(), "buy")) == 2
    assert "server decides" in client.render_port_summary(normalized)[1]


def test_port_info_preserves_authoritative_commodity_code_case():
    """The client must not rewrite the server's command identity."""
    response = {"status": "ok", "data": {"port": {"id": 7, "commodities": [
        {"code": "ore", "quantity": 12, "max_quantity": 100,
         "sells": True, "buys": True},
    ]}}}

    normalized = client.normalize_port_info(response)

    assert normalized["commodities"][0]["commodity"] == "ore"


def test_port_info_drops_commodity_with_malformed_fixed_direction():
    response = {"status": "ok", "data": {"port": {"id": 7, "commodities": [
        {"code": "ORE", "quantity": 12, "max_quantity": 100,
         "sells": "false", "buys": True},
        {"code": "ORG", "quantity": 12, "max_quantity": 100,
         "sells": True, "buys": False},
    ]}}}

    normalized = client.normalize_port_info(response)

    assert [row["commodity"] for row in normalized["commodities"]] == ["ORG"]
    assert client._trade_rows(
        type("Ctx", (), {"state": {"port_inventory": normalized}})(), "buy"
    )[0]["commodity"] == "ORG"


def test_trade_rows_rejects_malformed_cached_direction_metadata():
    """A malformed cached mode must not cross the trade command boundary."""
    ctx = type("Ctx", (), {"state": {"port_inventory": {
        "commodities": [
            {"commodity": "ORE", "sells": "false", "buys": True},
            {"commodity": "ORG", "sells": True, "buys": False},
        ],
    }}})()

    assert [row["commodity"] for row in client._trade_rows(ctx, "buy")] == ["ORG"]
    assert [row["commodity"] for row in client._trade_rows(ctx, "sell")] == ["ORE"]


def test_trade_rows_rejects_malformed_cached_commodity_identity():
    """A stale cache row must not create a malformed trade target."""
    ctx = type("Ctx", (), {"state": {"port_inventory": {
        "commodities": [
            {"commodity": None, "sells": True, "buys": True},
            {"commodity": "", "sells": True, "buys": True},
            {"commodity": " ORE ", "sells": True, "buys": True},
            {"commodity": "ORE", "sells": True, "buys": True},
        ],
    }}})()

    assert [row["commodity"] for row in client._trade_rows(ctx, "buy")] == ["ORE"]


def test_port_info_drops_commodity_with_malformed_authoritative_code():
    """Malformed server identity must never become an actionable trade code."""
    response = {"status": "ok", "data": {"port": {"id": 7, "commodities": [
        {"code": True, "quantity": 12, "max_quantity": 100,
         "sells": True, "buys": False},
        {"code": 42, "quantity": 12, "max_quantity": 100,
         "sells": True, "buys": False},
        {"code": " ore ", "quantity": 12, "max_quantity": 100,
         "sells": True, "buys": False},
        {"code": "ORE", "quantity": 12, "max_quantity": 100,
         "sells": True, "buys": False},
    ]}}}

    normalized = client.normalize_port_info(response)

    assert [row["commodity"] for row in normalized["commodities"]] == ["ORE"]


def test_port_info_does_not_treat_boolean_stock_as_quantity():
    """Malformed JSON booleans must not become actionable stock/capacity."""
    response = {"status": "ok", "data": {"port": {"id": 7, "commodities": [
        {"code": "ORE", "quantity": True, "max_quantity": False,
         "sells": True, "buys": True},
    ]}}}

    normalized = client.normalize_port_info(response)

    assert normalized["commodities"] == [{
        "commodity": "ORE", "available": None, "max_quantity": None,
        "base_price": None, "sells": True, "buys": True,
    }]


@pytest.mark.parametrize("malformed_price", [True, False, "40", {}, 40.5, -1])
def test_port_info_does_not_project_malformed_price_as_indicative_credit_value(malformed_price):
    response = {"status": "ok", "data": {"port": {"id": 7, "commodities": [
        {"code": "ORE", "quantity": 12, "max_quantity": 100,
         "price": malformed_price, "sells": True, "buys": True},
    ]}}}

    normalized = client.normalize_port_info(response)

    assert normalized["commodities"][0]["base_price"] is None
    assert "~?" in client.render_port_summary(normalized)[1]


@pytest.mark.parametrize("invalid_id", [None, True, 0, -4, "7"])
def test_port_info_does_not_cache_inventory_without_authoritative_id(invalid_id):
    response = {
        "status": "ok",
        "data": {"port": {"id": invalid_id, "name": "Unconfirmed", "commodities": []}},
    }

    assert client.normalize_port_info(response) == {}


@pytest.mark.parametrize("malformed_commodities", [None, {}, "ORE", 1])
def test_port_info_rejects_non_array_commodities_without_crashing(malformed_commodities):
    """A malformed port refresh must not leave a stale inventory actionable."""
    response = {
        "status": "ok",
        "type": "trade.port_info",
        "data": {"port": {"id": 7, "commodities": malformed_commodities}},
    }

    assert client.normalize_port_info(response) == {}


def test_quote_reconciliation_disconnect_fails_closed_without_raising(ctx_factory, monkeypatch, capsys):
    """A refused quote must remain recoverable if its refresh loses transport."""
    ctx = ctx_factory({"id": 7})
    ctx.state["port_inventory"] = {"id": 7, "commodities": [{"commodity": "ORE"}]}

    def lost_refresh(_ctx, _data=None):
        raise ConnectionError("socket closed")

    monkeypatch.setattr(client, "_refresh_port_info", lost_refresh)

    assert client._safe_refresh_port_info(ctx, {"port_id": 7, "sector_id": 9}) is False
    assert "port_inventory" not in ctx.state
    assert "connection lost" in ctx.state["trade_refresh_error"]
    assert "Reconnect before attempting another trade" in capsys.readouterr().out


def test_trade_refuses_without_authoritative_current_sector(ctx_factory, fake_conn, monkeypatch, capsys):
    """A malformed/stale location must not cross the trade protocol boundary."""
    ctx = ctx_factory({"id": True, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    monkeypatch.setattr("builtins.input", lambda _prompt: "1")

    client.dock_trade_flow(ctx, "buy")

    assert fake_conn.calls == []
    assert "authoritative current sector" in capsys.readouterr().out


def test_dock_context_requires_authoritative_stardock_type(ctx_factory, fake_conn):
    """A port name or sector number must not grant Stardock trade services."""
    ctx = ctx_factory({"id": 1, "port": {"id": 7}})
    fake_conn.queue("port.info", {
        "status": "ok",
        "type": "trade.port_info",
        "data": {"port": {
            "id": 7, "name": "Stardock", "type": 0,
            "commodities": [],
        }},
    })

    client.ctx_refresh_port_context(ctx)

    assert ctx.state["is_stardock"] is False


def test_dock_context_accepts_authoritative_stardock_type(ctx_factory, fake_conn):
    ctx = ctx_factory({"id": 42, "port": {"id": 7}})
    fake_conn.queue("port.info", {
        "status": "ok",
        "type": "trade.port_info",
        "data": {"port": {
            "id": 7, "name": "Ordinary Port", "type": 9,
            "commodities": [],
        }},
    })

    client.ctx_refresh_port_context(ctx)

    assert ctx.state["is_stardock"] is True


def test_port_refresh_rejects_explicit_cross_sector_port_record(ctx_factory, fake_conn):
    """A port response from another sector must not become trade context."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    fake_conn.queue("port.info", {
        "status": "ok",
        "type": "trade.port_info",
        "data": {"port": {
            "id": 7, "sector_id": 10, "name": "Wrong Sector",
            "commodities": [{"code": "ORE", "quantity": 12, "max_quantity": 100}],
        }},
    })

    response = client._refresh_port_info(ctx, {"port_id": 7, "sector_id": 9})

    assert response["status"] == "ok"
    assert "port_inventory" not in ctx.state
    assert "different sector" in ctx.state["port_info_error"]


def test_port_refresh_rejects_noncanonical_port_response_type(ctx_factory, fake_conn):
    """The port-info command aliases still publish one trade.port_info schema."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    fake_conn.queue("port.info", {
        "status": "ok",
        "type": "port.info",
        "data": {"port": {
            "id": 7, "name": "Wrong response contract", "commodities": [],
        }},
    })

    response = client._refresh_port_info(ctx, {"port_id": 7, "sector_id": 9})

    assert response["status"] == "ok"
    assert "port_inventory" not in ctx.state
    assert "unexpected port response type" in ctx.state["port_info_error"]


def test_port_refresh_rejects_omitted_port_response_type(ctx_factory, fake_conn):
    """An omitted type must not become a second actionable port contract."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    fake_conn.queue("port.info", {
        "status": "ok",
        "data": {"port": {
            "id": 7, "name": "Missing response type", "commodities": [],
        }},
    })

    response = client._refresh_port_info(ctx, {"port_id": 7, "sector_id": 9})

    assert response["status"] == "ok"
    assert "port_inventory" not in ctx.state
    assert "unexpected port response type" in ctx.state["port_info_error"]


def test_port_summary_uses_indicative_prices_and_port_name():
    lines = client.render_port_summary(client.normalize_port_info(PORT_RESPONSE))
    assert lines[0] == "--- Port: New Dawn ---"
    assert "ORE: stock 12/100; sell/buy; base ~40" in lines[1]


def test_port_summary_maps_server_directions_to_player_actions():
    normalized = client.normalize_port_info({
        "status": "ok",
        "data": {"port": {"id": 7, "name": "New Dawn", "commodities": [
            {"code": "ORE", "quantity": 12, "max_quantity": 100,
             "sells": True, "buys": False},
        ]}},
    })

    assert "ORE: stock 12/100; buy; base ~?" in client.render_port_summary(normalized)[1]


def test_cancelled_buy_quotes_but_does_not_mutate(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "type": "trade.quote", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 2,
        "buy_price": 40.0, "total_buy_price": 80,
    }})
    monkeypatch.setattr("builtins.input", lambda prompt: {"Commodity number: ": "1",
                                                            "Quantity of ORE: ": "2",
                                                                "Trade account [S]aved preference / [P]etty cash / [B]ank (default S): ": "",
                                                            "Commit this trade? [y/N]: ": ""}[prompt])
    client.dock_trade_flow(ctx, "buy")
    assert [c["command"] for c in fake_conn.calls] == ["trade.quote"]
    assert "cancelled" in capsys.readouterr().out.lower()


@pytest.mark.parametrize("malformed_data", [[], "not-an-object", 7])
def test_malformed_typed_quote_refuses_and_refreshes_before_mutation(
    ctx_factory, fake_conn, monkeypatch, capsys, malformed_data
):
    """A typed quote with non-object data must not crash or authorize settlement."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {
        "status": "ok", "type": "trade.quote", "data": malformed_data,
    })
    fake_conn.queue("port.info", PORT_RESPONSE)
    answers = iter(["1", "1", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    assert [call["command"] for call in fake_conn.calls] == ["trade.quote", "port.info"]
    assert "trade.buy" not in [call["command"] for call in fake_conn.calls]
    assert "invalid data" in capsys.readouterr().out


@pytest.mark.parametrize("field, value", [
    ("buy_price", "40"),
    ("sell_price", True),
    ("total_buy_price", float("nan")),
    ("total_sell_price", -1),
])
def test_typed_quote_with_malformed_price_refuses_before_settlement(
    ctx_factory, fake_conn, monkeypatch, capsys, field, value
):
    """Malformed quote money must not authorize a trade mutation."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    quote_data = {
        "port_id": 7, "commodity": "ORE", "quantity": 1,
        "buy_price": 40.0, "total_buy_price": 40,
    }
    quote_data[field] = value
    fake_conn.queue("trade.quote", {
        "status": "ok", "type": "trade.quote", "data": quote_data,
    })
    fake_conn.queue("port.info", PORT_RESPONSE)
    answers = iter(["1", "1", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    assert [call["command"] for call in fake_conn.calls] == ["trade.quote", "port.info"]
    assert "trade.buy" not in [call["command"] for call in fake_conn.calls]
    assert "Trade quote returned invalid data" in capsys.readouterr().out


def test_confirmed_buy_uses_uuid_and_refreshes_state(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "type": "trade.quote", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 2,
        "buy_price": 40.0, "total_buy_price": 80,
    }})
    fake_conn.queue("trade.buy", {"status": "ok", "type": "trade.buy_receipt_v1", "data": {
        "port_id": 7, "sector_id": 9, "player_id": 1,
        "total_cost": "82.00", "fees": "2.00", "credits_remaining": "18.00",
        "lines": [{"commodity": "ORE", "quantity": 2, "unit_price": "40.00"}],
    }})
    answers = iter(["1", "2", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))
    monkeypatch.setattr(uuid, "uuid4", lambda: uuid.UUID("12345678-1234-5678-1234-567812345678"))
    client.dock_trade_flow(ctx, "buy")
    calls = fake_conn.calls
    assert [c["command"] for c in calls] == ["trade.quote", "trade.buy", "player.my_info", "ship.status", "port.info"]
    assert calls[1]["data"]["idempotency_key"] == "12345678-1234-5678-1234-567812345678"
    assert "Total charged (including fees): 82" in capsys.readouterr().out


def test_successful_trade_receipt_does_not_claim_port_refresh_when_refused(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A receipt is shown, but stock remains explicitly un-reconciled."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "type": "trade.quote", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 1,
        "buy_price": 40, "total_buy_price": 40,
    }})
    fake_conn.queue("trade.buy", {"status": "ok", "type": "trade.buy_receipt_v1", "data": {
        "port_id": 7, "sector_id": 9, "player_id": 9,
        "total_cost": "40.00", "lines": [{"commodity": "ORE", "quantity": 1}],
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 9, "credits": 60}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 4, "cargo": []}}})
    fake_conn.queue("port.info", {"status": "error", "error": {"message": "port is no longer current"}})
    answers = iter(["1", "1", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    output = capsys.readouterr().out
    assert "Trade Receipt" in output
    assert "port stock could not be refreshed" in output
    assert ctx.state["trade_refresh_error"] == "port is no longer current"
    assert "port_inventory" not in ctx.state


def test_trade_refresh_does_not_claim_reconciliation_when_player_read_refused(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A successful port read cannot mask a failed balance refresh."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "type": "trade.quote", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 1,
        "buy_price": 40, "total_buy_price": 40,
    }})
    fake_conn.queue("trade.buy", {"status": "error", "error": {"message": "credits changed"}})
    fake_conn.queue("player.my_info", {"status": "error", "error": {"message": "refresh refused"}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 4}}})
    fake_conn.queue("port.info", PORT_RESPONSE)
    answers = iter(["1", "1", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    output = capsys.readouterr().out
    assert "authoritative player balance could not be refreshed" in output
    assert ctx.state["trade_refresh_error"] == "authoritative player balance refresh failed"
    assert "port_inventory" not in ctx.state
    assert "commodities" not in ctx.last_sector_desc["port"]


def test_trade_refresh_rejects_cross_account_or_ship_success(
    ctx_factory, fake_conn, capsys
):
    """Typed refreshes must remain bound to the session's known identities."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["player_id"] = 9
    ctx.hud = client.hud_module.HudState(
        sector_id=9, ship_id=4, credits=100, cargo_used=1, cargo_total=10
    )
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 99, "credits": "1.00"}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 88, "cargo": []}},
    })
    fake_conn.queue("port.info", {
        "status": "ok", "type": "trade.port_info",
        "data": {"port": {"id": 7, "commodities": []}},
    })

    assert client._refresh_trade_views(ctx, 7) is False
    assert ctx.hud.credits == 100
    assert ctx.hud.ship_id == 4
    assert "authoritative port stock could not be refreshed" in capsys.readouterr().out


def test_trade_refresh_rejects_unrelated_success_response_type(
    ctx_factory, fake_conn, capsys
):
    """A delayed response from another read cannot refresh trade HUD state."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.hud = ctx.hud.__class__(credits=100)
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "ship.status",
        "data": {"player": {"id": 9, "credits": "1.00"}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 4, "cargo": []}},
    })
    fake_conn.queue("port.info", {
        "status": "ok", "type": "trade.port_info",
        "data": {"port": {"id": 7, "commodities": []}},
    })

    assert client._refresh_trade_views(ctx, 7) is False
    assert ctx.hud.credits == 100
    assert "port stock could not be refreshed" in capsys.readouterr().out


def test_trade_refresh_rejects_untyped_balance_or_cargo_reads(
    ctx_factory, fake_conn, capsys
):
    """A trade receipt cannot authorize untyped post-settlement refreshes."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.hud = ctx.hud.__class__(credits=100)
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 9, "credits": "1.00"}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 4, "cargo": []}},
    })
    fake_conn.queue("port.info", {
        "status": "ok", "type": "trade.port_info",
        "data": {"port": {"id": 7, "commodities": []}},
    })

    assert client._refresh_trade_views(ctx, 7) is False
    assert ctx.hud.credits == 100
    assert "port stock could not be refreshed" in capsys.readouterr().out


def test_port_refresh_rejects_malformed_requested_sector_without_response_sector(
    ctx_factory, fake_conn, capsys
):
    """A malformed location selector must not make a port snapshot actionable."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    fake_conn.queue("port.info", {
        "status": "ok", "type": "trade.port_info",
        "data": {"port": {"id": 7, "commodities": []}},
    })

    response = client._refresh_port_info(ctx, {"port_id": 7, "sector_id": "9"})

    assert response["status"] == "ok"
    assert "port_inventory" not in ctx.state
    assert ctx.state["port_info_error"] == "The requested sector ID is not authoritative."
    assert "Port" not in capsys.readouterr().out


def test_buy_uses_authoritative_quote_commodity_casing(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A canonical quote may normalize the display commodity spelling."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 7, "commodity": "ore", "quantity": 1,
        "buy_price": 40, "total_buy_price": 40,
    }})
    fake_conn.queue("trade.buy", {"status": "ok", "data": {
        "port_id": 7, "sector_id": 9, "player_id": 9,
        "total_cost": "40.00", "lines": [
            {"commodity": "ore", "quantity": 1, "unit_price": "40.00"}
        ],
    }})
    answers = iter(["1", "1", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    trade = next(call for call in fake_conn.calls if call["command"] == "trade.buy")
    assert trade["data"]["items"] == [{"commodity": "ore", "quantity": 1}]
    assert "Trade Receipt (Bought)" in capsys.readouterr().out


def test_sell_receipt_describes_net_total(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 2,
        "sell_price": 35.0, "total_sell_price": 70,
    }})
    fake_conn.queue("trade.sell", {"status": "ok", "type": "trade.sell_receipt_v1", "data": {
        "port_id": 7, "sector_id": 9, "player_id": 1,
        "total_cost": "68.00", "fees": "2.00", "credits_remaining": "68.00",
        "lines": [{"commodity": "ORE", "quantity": 2}]
    }})
    monkeypatch.setattr("builtins.input", lambda _prompt: {"Commodity number: ": "1",
                                                            "Quantity of ORE: ": "2",
                                                                "Trade account [S]aved preference / [P]etty cash / [B]ank (default S): ": "",
                                                            "Commit this trade? [y/N]: ": "y"}[_prompt])
    client.dock_trade_flow(ctx, "sell")
    assert "Net credits received (after fees): 68" in capsys.readouterr().out


def test_mismatched_quote_is_not_used_for_trade(ctx_factory, fake_conn, monkeypatch, capsys):
    """A quote for another line cannot authorize a mutating trade request."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 8, "commodity": "ORG", "quantity": 99,
        "buy_price": 1, "total_buy_price": 99,
    }})
    fake_conn.queue("port.info", PORT_RESPONSE)
    answers = iter(["1", "2", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    output = capsys.readouterr().out
    assert "did not identify the requested port" in output
    assert [call["command"] for call in fake_conn.calls] == ["trade.quote", "port.info"]


def test_mismatched_quote_response_type_is_not_used_for_trade(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """An explicit non-quote envelope cannot authorize a mutating trade."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {
        "status": "ok", "type": "trade.sell_receipt_v1", "data": {
            "port_id": 7, "commodity": "ORE", "quantity": 1,
            "buy_price": 40, "total_buy_price": 40,
        },
    })
    fake_conn.queue("port.info", PORT_RESPONSE)
    answers = iter(["1", "1", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    output = capsys.readouterr().out
    assert "unexpected response type" in output
    assert [call["command"] for call in fake_conn.calls] == ["trade.quote", "port.info"]


def test_trade_receipt_requires_authoritative_settlement_lines(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A typed success without the contract's settlement line is not a receipt."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 1,
        "buy_price": 40, "total_buy_price": 40,
    }})
    fake_conn.queue("trade.buy", {"status": "ok", "data": {
        "port_id": 7, "sector_id": 9, "player_id": 9,
        "total_cost": "40.00", "lines": [],
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"credits": 60}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "cargo": []}}})
    fake_conn.queue("port.info", PORT_RESPONSE)
    answers = iter(["1", "1", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    output = capsys.readouterr().out
    assert "settlement was not displayed" in output
    assert "Trade Receipt" not in output
    assert [call["command"] for call in fake_conn.calls] == [
        "trade.quote", "trade.buy", "player.my_info", "ship.status", "port.info",
    ]


def test_confirmed_buy_can_use_bank_account(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 2,
        "buy_price": 40.0, "total_buy_price": 80,
    }})
    fake_conn.queue("trade.buy", {"status": "ok", "data": {
        "port_id": 7, "sector_id": 9,
        "total_cost": "82.00", "fees": "2.00", "credits_remaining": "18.00",
        "lines": [{"commodity": "ORE", "quantity": 2}],
    }})
    answers = iter(["1", "2", "b", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))
    client.dock_trade_flow(ctx, "buy")
    trade = next(call for call in fake_conn.calls if call["command"] == "trade.buy")
    assert trade["data"]["account"] == 1
    assert "Trade account: bank." in capsys.readouterr().out


def test_confirmed_buy_can_use_saved_server_trade_preference(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 2,
        "buy_price": 40.0, "total_buy_price": 80,
    }})
    fake_conn.queue("trade.buy", {"status": "ok", "data": {
        "port_id": 7, "sector_id": 9,
        "total_cost": "80.00", "credits_remaining": "20.00",
        "lines": [{"commodity": "ORE", "quantity": 2}],
    }})
    answers = iter(["1", "2", "s", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    trade = next(call for call in fake_conn.calls if call["command"] == "trade.buy")
    assert "account" not in trade["data"]
    assert "saved server preference" in capsys.readouterr().out


def test_confirmed_buy_defaults_to_saved_server_trade_preference(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """Pressing Enter must preserve the server's saved account preference."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "type": "trade.quote", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 1,
        "buy_price": 40, "total_buy_price": 40,
    }})
    fake_conn.queue("trade.buy", {"status": "ok", "type": "trade.buy_receipt_v1", "data": {
        "port_id": 7, "sector_id": 9, "total_cost": "40.00",
        "lines": [{"commodity": "ORE", "quantity": 1}],
    }})
    answers = iter(["1", "1", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    trade = next(call for call in fake_conn.calls if call["command"] == "trade.buy")
    assert "account" not in trade["data"]
    output = capsys.readouterr().out
    assert "default S" in output


def test_mismatched_trade_receipt_is_not_presented_and_refreshes_state(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A receipt for another port/sector cannot be shown as this trade's settlement."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 1,
        "buy_price": 40, "total_buy_price": 40,
    }})
    fake_conn.queue("trade.buy", {"status": "ok", "data": {
        "port_id": 8, "sector_id": 9, "total_cost": "40.00", "lines": [],
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"credits": 100}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "cargo": []}}})
    fake_conn.queue("port.info", PORT_RESPONSE)
    monkeypatch.setattr("builtins.input", lambda prompt: {
        "Commodity number: ": "1",
        "Quantity of ORE: ": "1",
        "Trade account [S]aved preference / [P]etty cash / [B]ank (default S): ": "",
        "Commit this trade? [y/N]: ": "y",
    }[prompt])

    client.dock_trade_flow(ctx, "buy")

    output = capsys.readouterr().out
    assert "settlement was not displayed" in output
    assert "Trade Receipt" not in output
    assert [call["command"] for call in fake_conn.calls] == [
        "trade.quote", "trade.buy", "player.my_info", "ship.status", "port.info",
    ]


def test_mismatched_trade_receipt_type_is_not_presented_and_refreshes_state(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 1,
        "buy_price": 40, "total_buy_price": 40,
    }})
    fake_conn.queue("trade.buy", {"status": "ok", "type": "trade.sell_receipt_v1", "data": {
        "port_id": 7, "sector_id": 9, "total_cost": "40.00", "lines": [],
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"credits": 20}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "cargo": []}}})
    fake_conn.queue("port.info", PORT_RESPONSE)
    answers = iter(["1", "1", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    output = capsys.readouterr().out
    assert "unexpected receipt type" in output
    assert "Trade Receipt" not in output
    assert [call["command"] for call in fake_conn.calls] == [
        "trade.quote", "trade.buy", "player.my_info", "ship.status", "port.info",
    ]


def test_mismatched_trade_receipt_player_is_not_presented_and_refreshes_state(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A receipt for another authenticated player cannot be shown as settlement."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    ctx.state["player_id"] = 14
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 1,
        "buy_price": 40, "total_buy_price": 40,
    }})
    fake_conn.queue("trade.buy", {"status": "ok", "data": {
        "port_id": 7, "sector_id": 9, "player_id": 99,
        "total_cost": "40.00", "lines": [],
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 14}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "cargo": []}}})
    fake_conn.queue("port.info", PORT_RESPONSE)
    answers = iter(["1", "1", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    output = capsys.readouterr().out
    assert "confirmed player" in output
    assert "Trade Receipt" not in output
    assert [call["command"] for call in fake_conn.calls] == [
        "trade.quote", "trade.buy", "player.my_info", "ship.status", "port.info",
    ]


def test_trade_receipt_without_player_id_is_not_presented_when_session_identity_is_known(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A receipt missing required player identity cannot authorize settlement."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    ctx.state["player_id"] = 14
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 1,
        "buy_price": 40, "total_buy_price": 40,
    }})
    fake_conn.queue("trade.buy", {"status": "ok", "data": {
        "port_id": 7, "sector_id": 9, "total_cost": "40.00", "lines": [],
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 14}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "cargo": []}}})
    fake_conn.queue("port.info", PORT_RESPONSE)
    answers = iter(["1", "1", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    output = capsys.readouterr().out
    assert "confirmed player" in output
    assert "Trade Receipt" not in output
    assert [call["command"] for call in fake_conn.calls] == [
        "trade.quote", "trade.buy", "player.my_info", "ship.status", "port.info",
    ]


def test_mismatched_trade_receipt_line_is_not_presented_and_refreshes_state(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 1,
        "buy_price": 40, "total_buy_price": 40,
    }})
    fake_conn.queue("trade.buy", {"status": "ok", "data": {
        "port_id": 7, "sector_id": 9, "total_cost": "40.00",
        "lines": [{"commodity": "ORG", "quantity": 1}],
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"credits": 20}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "cargo": []}}})
    fake_conn.queue("port.info", PORT_RESPONSE)
    monkeypatch.setattr("builtins.input", lambda prompt: {
        "Commodity number: ": "1", "Quantity of ORE: ": "1",
        "Trade account [S]aved preference / [P]etty cash / [B]ank (default S): ": "",
        "Commit this trade? [y/N]: ": "y",
    }[prompt])

    client.dock_trade_flow(ctx, "buy")

    output = capsys.readouterr().out
    assert "settlement was not displayed" in output
    assert "Trade Receipt" not in output


def test_refused_trade_refreshes_authoritative_state_without_claiming_mutation(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 2,
        "buy_price": 40.0, "total_buy_price": 80,
    }})
    fake_conn.queue("trade.buy", {"status": "refused", "error": {"message": "Stock exhausted"}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"credits": 20}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9, "cargo": []}}})
    fake_conn.queue("port.info", PORT_RESPONSE)
    answers = iter(["1", "2", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    assert [call["command"] for call in fake_conn.calls] == [
        "trade.quote", "trade.buy", "player.my_info", "ship.status", "port.info",
    ]
    assert ctx.state["last_rpc"]["status"] == "refused"
    assert ctx.state["port_inventory"]["name"] == "New Dawn"
    assert "Stock exhausted" in capsys.readouterr().out


def test_trade_disconnect_does_not_claim_or_retry_mutation(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A lost settlement reply must wait for authoritative reconnect refresh."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 2,
        "buy_price": 40.0, "total_buy_price": 80,
    }})

    original_rpc = fake_conn.rpc

    def disconnect_on_trade(command, data):
        if command == "trade.buy":
            raise ConnectionError("socket closed")
        return original_rpc(command, data)

    monkeypatch.setattr(fake_conn, "rpc", disconnect_on_trade)
    answers = iter(["1", "2", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    assert [call["command"] for call in fake_conn.calls] == ["trade.quote"]
    assert ctx.state["last_rpc"] is None
    assert "port_inventory" not in ctx.state
    assert "commodities" not in ctx.last_sector_desc["port"]
    assert "uncertain" in ctx.state["trade_refresh_error"]
    output = capsys.readouterr().out
    assert "outcome is uncertain" in output
    assert "Reconnect before attempting another trade" in output
    assert "Trade Receipt" not in output


def test_quote_disconnect_does_not_crash_or_open_settlement(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A lost quote reply must not cross into the mutating trade command."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)

    def disconnect_on_quote(command, data):
        if command == "trade.quote":
            raise ConnectionError("socket closed during quote")
        return fake_conn.rpc(command, data)

    monkeypatch.setattr(fake_conn, "rpc", disconnect_on_quote)
    answers = iter(["1", "2", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    assert fake_conn.calls == []
    assert ctx.state["last_rpc"] is None
    assert "port_inventory" not in ctx.state
    assert "commodities" not in ctx.last_sector_desc["port"]
    assert "uncertain" in ctx.state["trade_refresh_error"]
    output = capsys.readouterr().out
    assert "quote could not be confirmed" in output
    assert "Reconnect before attempting another trade" in output
    assert "Trade Receipt" not in output


def test_trade_refresh_disconnect_does_not_crash_or_show_stale_summary(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A confirmed trade still requires reconnect when reconciliation is lost."""
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "ok", "type": "trade.quote", "data": {
        "port_id": 7, "commodity": "ORE", "quantity": 2,
        "buy_price": 40.0, "total_buy_price": 80,
    }})
    fake_conn.queue("trade.buy", {"status": "ok", "type": "trade.buy_receipt_v1", "data": {
        "port_id": 7, "sector_id": 9, "player_id": 1,
        "lines": [{"commodity": "ORE", "quantity": 2}],
        "total_cost": 80,
    }})
    original_rpc = fake_conn.rpc

    def disconnect_on_refresh(command, data):
        if command == "player.my_info":
            raise ConnectionError("socket closed during refresh")
        return original_rpc(command, data)

    monkeypatch.setattr(fake_conn, "rpc", disconnect_on_refresh)
    answers = iter(["1", "2", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    assert ctx.state["last_rpc"]["status"] == "ok"
    assert "trade_refresh_error" in ctx.state
    output = capsys.readouterr().out
    assert "authoritative balance, cargo, or port stock" in output
    assert "Trade Receipt" in output
    assert "Port:" not in output


def test_refused_quote_refreshes_authoritative_port_without_claiming_trade(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {"status": "refused", "error": {"message": "Port moved"}})
    fake_conn.queue("port.info", PORT_RESPONSE)
    answers = iter(["1", "2", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    assert [call["command"] for call in fake_conn.calls] == ["trade.quote", "port.info"]
    assert ctx.state["port_inventory"]["name"] == "New Dawn"
    assert "Port moved" in capsys.readouterr().out


@pytest.mark.parametrize("invalid_port_id", [True, 0, -7, 7.5, "7"])
def test_invalid_authoritative_port_id_sends_no_trade_request(
    ctx_factory, fake_conn, monkeypatch, capsys, invalid_port_id
):
    """A display row must not turn an invalid server ID into a trade command."""
    ctx = ctx_factory({"id": 9, "port": {"id": invalid_port_id, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    ctx.state["port_inventory"]["id"] = invalid_port_id
    answers = iter(["1", "1", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.dock_trade_flow(ctx, "buy")

    assert fake_conn.calls == []
    assert "valid authoritative port ID" in capsys.readouterr().out


def test_invalid_trade_account_cancels_before_quote(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    answers = iter(["1", "2", "corporation"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))
    client.dock_trade_flow(ctx, "buy")
    assert fake_conn.calls == []
    assert "choose petty cash, bank, or saved preference" in capsys.readouterr().out


def test_interactive_buy_uses_canonical_quoted_port_flow(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory({"id": 9, "port": {"id": 7, "name": "New Dawn"}})
    ctx.state["port_inventory"] = client.normalize_port_info(PORT_RESPONSE)
    fake_conn.queue("trade.quote", {
        "status": "refused",
        "error": {"message": "fixed mode refuses this commodity"},
    })
    fake_conn.queue("port.info", PORT_RESPONSE)
    answers = iter(["1", "2", "", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.interactive_buy_handler(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["trade.quote", "port.info"]
    assert "fixed mode refuses" in capsys.readouterr().out
