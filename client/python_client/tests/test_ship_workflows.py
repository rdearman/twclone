"""Ship and StarDock flows use only server-listed hardware and hull contracts."""
import client


def test_shipyard_upgrade_refreshes_independently_and_preserves_refused_sector(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    old_sector = {"id": 7, "port": {"id": 3}}
    ctx = ctx_factory(sector=old_sector)
    ctx.state["towing_ship_id"] = 88
    fake_conn.queue("shipyard.list", {"status": "ok", "data": {"available": [
        {"type_id": 2, "name": "Scout", "net_cost": 500, "eligible": True, "reasons": []},
    ]}})
    fake_conn.queue("shipyard.upgrade", {"status": "ok", "data": {
        "ship_id": 12, "new_type_id": 2, "new_ship_name": "Scout One", "credits_spent": 500,
    }})
    fake_conn.queue("player.my_info", {"status": "refused", "error": {"message": "Unavailable"}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"hull": 100}}})
    fake_conn.queue("move.describe_sector", {"status": "refused", "error": {"message": "Unavailable"}})
    answers = iter(["1", "Scout One", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.shipyard_upgrade_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "shipyard.list", "shipyard.upgrade", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert fake_conn.calls[1]["data"] == {"new_type_id": 2, "new_ship_name": "Scout One"}
    assert ctx.last_sector_desc == old_sector
    assert "towing_ship_id" not in ctx.state
    output = capsys.readouterr().out
    assert "displayed credits may be stale" in output
    assert "displayed sector may be stale" in output


def test_shipyard_upgrade_refusal_does_not_claim_success_or_change_state(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 7})
    fake_conn.queue("shipyard.list", {"status": "ok", "data": {"available": [
        {"type_id": 2, "name": "Scout", "net_cost": 500, "eligible": True, "reasons": []},
    ]}})
    fake_conn.queue("shipyard.upgrade", {"status": "refused", "error": {"message": "Insufficient credits"}})
    answers = iter(["1", "Scout One", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.shipyard_upgrade_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["shipyard.list", "shipyard.upgrade"]
    assert ctx.last_sector_desc == {"id": 7}
    output = capsys.readouterr().out
    assert "Insufficient credits" in output
    assert "Successfully upgraded" not in output


def test_shipyard_upgrade_requires_confirmation_and_explains_tax(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("shipyard.list", {"status": "ok", "data": {"available": [
        {"type_id": 2, "name": "Scout", "net_cost": 500, "eligible": True, "reasons": []},
    ]}})
    answers = iter(["1", "Scout One", ""])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.shipyard_upgrade_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["shipyard.list"]
    output = capsys.readouterr().out
    assert "Net before tax: 500" in output
    assert "excludes tax" in output
    assert "no exchange was sent" in output


def test_ship_card_displays_authoritative_hull_and_marks_older_server_field_missing():
    current = client.render_ship_card({"id": 12, "name": "Wayfarer", "hull": 73})
    older = client.render_ship_card({"id": 12, "name": "Wayfarer"})

    assert "Hull:   73/100" in current
    assert "Hull:   —/100" in older


def test_ship_card_does_not_render_invalid_hull_as_authoritative():
    for hull in (True, -1, 101):
        rendered = client.render_ship_card({"hull": hull})
        assert "Hull:   —/100" in rendered


def test_hardware_purchase_selects_catalogue_item_and_refreshes_ship(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("hardware.list", {"status": "ok", "data": {"hardware": [
        {"item_code": "SHIELDS", "name": "Shield generator", "price": 100},
        {"item_code": "TRANSWARP", "name": "Transwarp drive", "price": 500},
    ]}})
    fake_conn.queue("hardware.buy", {"status": "ok", "data": {"quantity": 2, "item_code": "TRANSWARP"}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"credits": 900}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {
        "holds": 20, "fighters": 1, "shields": 2, "equipment": ["TRANSWARP"],
    }}})
    answers = iter(["2", "2"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.hardware_buy_flow(ctx)

    assert fake_conn.calls[0] == {"command": "hardware.list", "data": {}}
    assert fake_conn.calls[1] == {"command": "hardware.buy", "data": {
        "item_code": "TRANSWARP", "quantity": 2,
    }}
    assert [call["command"] for call in fake_conn.calls[2:]] == ["player.my_info", "ship.status"]
    assert "Successfully purchased 2 units of TRANSWARP" in capsys.readouterr().out


def test_hardware_purchase_rejects_item_not_in_server_catalogue(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("hardware.list", {"status": "ok", "data": {"hardware": [
        {"item_code": "SHIELDS", "name": "Shield generator", "price": 100},
    ]}})
    monkeypatch.setattr("builtins.input", lambda *_: "UNLISTED_DRIVE")

    client.hardware_buy_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["hardware.list"]
    assert "choose a hardware item from the server catalogue" in capsys.readouterr().out


def test_hardware_catalogue_refusal_is_presented_without_mutation(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("hardware.list", {
        "status": "refused", "error": {"message": "Dock at a supported hardware port."},
    })

    client.hardware_buy_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["hardware.list"]
    assert "Dock at a supported hardware port" in capsys.readouterr().out


def test_jettison_requires_confirmation_and_refreshes_authoritative_cargo(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("ship.info", {"status": "ok", "data": {"ship": {
        "cargo": [{"commodity": "ore", "quantity": 5}],
    }}})
    fake_conn.queue("ship.jettison", {"status": "ok", "data": {"remaining_cargo": [
        {"commodity": "ore", "quantity": 2},
    ]}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"credits": 100}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {
        "holds": 20, "cargo": [{"commodity": "ore", "qty": 2}],
    }}})
    answers = iter(["ORE", "3", "y"])
    prompts = []
    monkeypatch.setattr("builtins.input", lambda prompt: (prompts.append(prompt), next(answers))[1])

    client.jettison_cargo_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "ship.info", "ship.jettison", "player.my_info", "ship.status",
    ]
    assert fake_conn.calls[1]["data"] == {"commodity": "ORE", "quantity": 3}
    assert any("spend 1 turn" in prompt for prompt in prompts)
    assert "Successfully jettisoned" in capsys.readouterr().out
    assert ctx.hud.cargo_used == 2


def test_jettison_decline_does_not_send_turn_consuming_command(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("ship.info", {"status": "ok", "data": {"ship": {
        "cargo": [{"commodity": "ore", "quantity": 5}],
    }}})
    answers = iter(["ORE", "2", "n"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.jettison_cargo_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.info"]
    assert "Jettison cancelled" in capsys.readouterr().out


def test_jettison_over_reported_cargo_is_rejected_before_turn_spending(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("ship.info", {"status": "ok", "data": {"ship": {
        "cargo": [{"commodity": "ore", "quantity": 2}],
    }}})
    answers = iter(["ORE", "3"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.jettison_cargo_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.info"]
    assert "no jettison was sent" in capsys.readouterr().out


def test_claim_selected_ship_refreshes_independently_and_preserves_failed_sector(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    sector = {"id": 7, "ships": [{"id": 20, "name": "Drifter", "owner": "derelict"}]}
    ctx = ctx_factory(sector=sector)
    ctx.player_info = {"player": {"ship_id": 10}}
    ctx.state["towing_ship_id"] = 88
    fake_conn.queue("ship.claim", {"status": "ok", "data": {"ship": {"id": 20}}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"ship_id": 20}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"hull": 73}}})
    fake_conn.queue("move.describe_sector", {"status": "refused", "error": {"message": "Unavailable"}})
    answers = iter(["", "c", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.enter_ship_menu(ctx)

    assert fake_conn.calls[0] == {"command": "ship.claim", "data": {"ship_id": 20}}
    assert [call["command"] for call in fake_conn.calls[1:]] == ["player.my_info", "ship.status", "move.describe_sector"]
    assert client.get_my_ship_id(fake_conn, ctx) == 20
    assert ctx.last_sector_desc == sector
    assert "towing_ship_id" not in ctx.state
    assert "displayed sector may be stale" in capsys.readouterr().out


def test_invalid_claim_selection_never_defaults_to_first_ship(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory(sector={"id": 7, "ships": [{"id": 20, "owner": "derelict"}]})
    ctx.player_info = {"player": {"ship_id": 10}}
    monkeypatch.setattr("builtins.input", lambda *_: "99")

    client.enter_ship_menu(ctx)

    assert fake_conn.calls == []
    assert "no ship selected" in capsys.readouterr().out


def test_boardable_ship_requires_usable_id_and_known_owner_shape():
    assert not client.has_boardable_ship([{"name": "Unknown"}, {"id": 0}, {"id": 5, "owner": {"name": "Alice"}}])
    assert client.has_boardable_ship([{"id": 20, "owner": "derelict"}], my_ship_id=10)


def test_player_id_is_not_treated_as_current_ship_id(ctx_factory, fake_conn):
    ctx = ctx_factory()
    ctx.player_info = {"player": {"id": 20}, "ship": {"id": 42}}
    assert client.get_my_ship_id(fake_conn, ctx) == 42
    ctx.player_info = {"player": {"id": 20, "ship_id": True}}
    assert client.get_my_ship_id(fake_conn, ctx) is None


def test_owned_spare_ship_is_selectable_but_current_and_other_owned_ships_are_not():
    ships = [{"id": 10, "owner": "Captain"}, {"id": 20, "owner": "Captain"}]
    assert client.has_boardable_ship(ships, my_ship_id=10, my_name="Captain")
    assert not client.has_boardable_ship([ships[0]], my_ship_id=10, my_name="Captain")
    assert not client.has_boardable_ship([{"id": 30, "owner": "Other"}], my_ship_id=10, my_name="Captain")


def test_claim_refusal_does_not_refresh_or_replace_state(ctx_factory, fake_conn, monkeypatch, capsys):
    sector = {"id": 7, "ships": [{"id": 20, "owner": "derelict"}]}
    ctx = ctx_factory(sector=sector)
    ctx.player_info = {"player": {"ship_id": 10}}
    fake_conn.queue("ship.claim", {"status": "refused", "error": {"message": "Ship not claimable"}})
    answers = iter(["", "c", "y", "q"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.enter_ship_menu(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.claim"]
    assert ctx.player_info == {"player": {"ship_id": 10}}
    assert ctx.last_sector_desc == sector
    assert "Ship not claimable" in capsys.readouterr().out


def test_owned_spare_ship_can_be_selected_and_claimed(ctx_factory, fake_conn, monkeypatch):
    ctx = ctx_factory(sector={"id": 7, "ships": [
        {"id": 10, "name": "Current", "owner": "Captain"},
        {"id": 20, "name": "Spare", "owner": "Captain"},
    ]})
    ctx.player_info = {"player": {"ship_id": 10, "name": "Captain"}}
    fake_conn.queue("ship.claim", {"status": "ok", "data": {"ship": {"id": 20}}})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"ship_id": 20}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 20}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 7, "ships": []}})
    answers = iter(["", "c", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.enter_ship_menu(ctx)

    assert fake_conn.calls[0] == {"command": "ship.claim", "data": {"ship_id": 20}}
    assert client.get_my_ship_id(fake_conn, ctx) == 20
