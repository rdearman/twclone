extends SceneTree

const PortWorkflow = preload("res://PortWorkflow.gd")

var failures: Array[String] = []
var captured: Dictionary = {}
var tavern_enters := 0
var tavern_exits := 0
var tavern_requests := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	root.size = Vector2i(1400, 900)
	var view = PortWorkflow.new()
	root.add_child(view)
	await process_frame
	_check(view.get_node("WorkflowDim").offset_left == PortWorkflow.COMMAND_RAIL_CLEARANCE, "port/Tavern overlay covers the persistent command rail")
	_check(view.get_node("WorkflowMargin").offset_left > PortWorkflow.COMMAND_RAIL_CLEARANCE, "port/Tavern content does not begin beyond the command rail")
	view.trade_requested.connect(func(direction: String, port_id: int, sector_id: int, commodity: String, quantity: int, account: int) -> void:
		captured = {"direction": direction, "port_id": port_id, "sector_id": sector_id, "commodity": commodity, "quantity": quantity, "account": account}
	)
	view.tavern_entered.connect(func() -> void: tavern_enters += 1)
	view.tavern_exited.connect(func() -> void: tavern_exits += 1)
	view.tavern_enter_requested.connect(func() -> void: tavern_requests += 1)
	view.show_port({"port": {"id": 7, "name": "Helix", "commodities": [
		{"code": "ORE", "quantity": 8.0, "max_quantity": 30.0, "price": "40.00"},
		{"code": "ORG", "quantity": 0.0, "max_quantity": 30.0, "price": "50.00"},
	]}}, 17)
	_check(view.visible, "port screen did not open after supplied server data")
	_check(view._commodity_list.get_child_count() == 2, "port commodities were not composed into rows")
	view.show_port({"port": {"id": 7, "name": "Fixed modes", "commodities": [
		{"code": "ORE", "quantity": 8, "max_quantity": 30, "sells": false, "buys": true}
	]}}, 17)
	_check(view._trade_buttons.size() == 2, "fixed-mode commodity did not compose both trade controls")
	_check(view._trade_buttons[0]["button"].disabled, "BUY remained enabled when the authoritative port sells flag was false")
	_check(not view._trade_buttons[1]["button"].disabled, "SELL was disabled despite the authoritative port buys flag")
	view._trade_dialog.hide()
	view._choose_trade("buy", "ORE", {"code": "ORE", "sells": false, "buys": true})
	_check(not view._trade_dialog.visible, "stale/programmatic BUY selection bypassed the authoritative fixed-mode refusal")
	view.show_port({"port": {"id": 7, "name": "Unknown modes", "commodities": [
		{"code": "ORG", "quantity": 8, "max_quantity": 30}
	]}}, 17)
	_check(not view._trade_buttons[0]["button"].disabled and not view._trade_buttons[1]["button"].disabled,
		"trade action was hidden even though the authoritative schema omitted fixed direction")
	view.show_port({"port": {"id": 7, "name": "Malformed modes", "commodities": [
		{"code": "ORE", "quantity": 8, "max_quantity": 30, "sells": "false", "buys": 1}
	]}}, 17)
	_check(view._trade_buttons[0]["button"].disabled and view._trade_buttons[1]["button"].disabled, "malformed fixed-mode flags were coerced into executable trade actions")
	view._trade_dialog.hide()
	view._choose_trade("sell", "ORE", {"code": "ORE", "sells": "false", "buys": 1})
	_check(not view._trade_dialog.visible, "malformed fixed-mode metadata opened a trade quote dialog")
	view.show_port({"port": {"id": 7, "name": "Malformed commodities", "commodities": [
		{"code": true, "quantity": 8, "max_quantity": 30},
		{"code": 42, "quantity": 8, "max_quantity": 30},
		{"code": "   ", "quantity": 8, "max_quantity": 30},
		{"code": "ore", "quantity": 8, "max_quantity": 30}
	]}}, 17)
	_check(view._commodity_list.get_child_count() == 1, "malformed commodity identities were rendered as tradeable rows")
	view.show_port({"port": {"id": 7, "name": "Whitespace commodity", "commodities": [
		{"code": " ORE ", "quantity": 8, "max_quantity": 30},
		{"code": "ORG", "quantity": 8, "max_quantity": 30}
	]}}, 17)
	_check(view._commodity_list.get_child_count() == 1 and view._commodity_list.get_child(0).get_child_count() > 0,
		"whitespace-padded commodity identity was normalized into a tradeable row")
	_check(view._authoritative_commodity_code({"code": " ORE "}) == "",
		"whitespace-padded commodity identity was rewritten instead of rejected")
	_check(view._authoritative_price({"price": 40}) == 40,
		"valid authoritative port price was hidden")
	_check(view._authoritative_price({"price": 40.0}) == 40,
		"integral JSON-real port price was hidden")
	_check(view._authoritative_price({"buy_price": 45, "sell_price": 38}) == 45,
		"authoritative trade.port_info buy price was hidden")
	_check(view._authoritative_price({"sell_price": 38}) == 38,
		"authoritative trade.port_info sell price was hidden")
	_check(view._authoritative_price({"price": true}) == null,
		"boolean port price was projected as an indicative credit value")
	_check(view._authoritative_price({"price": "40"}) == null,
		"string port price was projected as an indicative credit value")
	_check(view._authoritative_price_label({"buy_price": 45, "sell_price": 38}) == "BUY 45 · SELL 38",
		"port row displayed one directional price for both trade actions")
	_check(view._authoritative_price_label({"buy_price": 45}) == "BUY 45 · SELL —",
		"port row did not preserve an undisclosed sell price")
	view._choose_trade("buy", "ORE", {"code": "ORE", "quantity": 8, "max_quantity": 30})
	# A port refresh supersedes an open quantity dialog; the old row must not
	# cross the protocol boundary after its authoritative inventory changed.
	view.show_port({"port": {"id": 7, "name": "Refreshed port", "commodities": [
		{"code": "ORG", "quantity": 8, "max_quantity": 30, "sells": true, "buys": false}
	]}}, 17)
	var stale_capture := captured.duplicate(true)
	view._quantity.value = 2
	view._submit_trade()
	_check(captured == stale_capture and not view._trade_dialog.visible, "stale trade dialog submitted a superseded commodity after port refresh")
	view.show_port({"port": {"id": 7, "name": "Helix", "commodities": [
		{"code": "ORE", "quantity": 8, "max_quantity": 30}
	]}}, 17)
	view._choose_trade("buy", "ORE", {"code": "ORE", "quantity": 8, "max_quantity": 30})
	view._quantity.value = 3
	view._submit_trade()
	_check(captured.get("direction") == "buy" and captured.get("port_id") == 7 and captured.get("sector_id") == 17, "trade selection lost authoritative context")
	_check(captured.get("commodity") == "ORE" and captured.get("quantity") == 3, "trade quantity selection was not preserved")
	_check(captured.get("account") == -1, "trade did not default to the saved server preference")
	view._choose_trade("buy", "ORE", {"code": "ORE", "quantity": 1, "max_quantity": 2})
	_check(view._quantity.max_value == 1000000, "cached port stock incorrectly capped the server-authoritative trade quantity")
	view._quantity.value = 500
	view._submit_trade()
	_check(captured.get("quantity") == 500, "trade quantity was clamped by stale port stock before the server quote")
	view._choose_trade("buy", "ORE", {"code": "ORE", "quantity": 8, "max_quantity": 30})
	view._trade_account.select(0)
	view._quantity.value = 1
	view._submit_trade()
	_check(captured.get("account") == -1, "server trade-account preference was not exposed as an explicit option")
	view._choose_trade("sell", "ORE", {"code": "ORE", "quantity": 8, "max_quantity": 30})
	view._trade_account.select(1)
	view._quantity.value = 1
	view._submit_trade()
	_check(captured.get("account") == 1, "bank account selection was not passed to trade workflow")
	view.close_for_location_change()
	view.show_port({"port": {"id": 7, "name": "Malformed location", "commodities": [
		{"code": "ORE", "quantity": 8, "max_quantity": 30, "sells": true, "buys": true}
	]}}, 17.0)
	_check(not view.visible, "malformed sector identity opened an actionable port workflow")
	_check(view._authoritative_port_id() == 0, "malformed sector identity retained an actionable port ID")
	view.show_port({"port": {"id": 7, "sector": 18, "name": "Stale location", "commodities": [
		{"code": "ORE", "quantity": 8, "max_quantity": 30, "sells": true, "buys": true}
	]}}, 17)
	_check(not view.visible and view._authoritative_port_id() == 0,
		"stale port.info sector opened an actionable workflow")
	view.show_port({"port": {"id": 7, "sector": "17", "name": "Malformed location", "commodities": [
		{"code": "ORE", "quantity": 8, "max_quantity": 30, "sells": true, "buys": true}
	]}}, 17)
	_check(not view.visible and view._authoritative_port_id() == 0,
		"malformed port.info sector opened an actionable workflow")
	for malformed_id in [true, 7.0, "7", 0, -7]:
		view.show_port({"port": {"id": malformed_id, "name": "Malformed", "commodities": [{"code": "ORE", "quantity": 8, "max_quantity": 30}]}}, 17)
		var captured_before := captured.duplicate(true)
		view._choose_trade("buy", "ORE", {"code": "ORE", "quantity": 8, "max_quantity": 30})
		view._quantity.value = 1
		view._submit_trade()
		_check(captured == captured_before, "malformed port ID was coerced into a trade request: %s" % str(malformed_id))
	_check(not view._tavern_button.visible, "a non-StarDock port incorrectly exposed Tavern access")
	view.show_port({"port": {"id": 8, "name": "Old port payload", "commodities": []}}, 17)
	_check(not view._tavern_button.visible, "a port without an authoritative type incorrectly exposed Tavern access")
	view.show_port({"port": {"id": 8, "name": "Federation StarDock", "type": 0, "commodities": []}}, 17)
	_check(not view._tavern_button.visible, "a StarDock-like name granted Tavern access without the server StarDock type")
	view.show_port({"port": {"id": 8, "name": "Black Market", "type": 10, "commodities": []}}, 17)
	_check(not view._tavern_button.visible, "a black-market port incorrectly exposed StarDock Tavern access")
	for malformed_type in [true, 9.0, "9"]:
		view.show_port({"port": {"id": 8, "name": "Malformed StarDock", "type": malformed_type, "commodities": []}}, 17)
		_check(not view._tavern_button.visible, "malformed port type unlocked StarDock Tavern access: %s" % str(malformed_type))
	view.show_port({"port": {"id": 8, "name": "Helix", "type": 9, "commodities": []}}, 17)
	_check(view._tavern_button.visible, "StarDock did not expose the Enter Tavern navigation control")
	view._tavern_button.pressed.emit()
	_check(tavern_requests == 1 and tavern_enters == 0 and not view._tavern_mode and view._commodity_region.visible, "Tavern entry bypassed the server presence check")
	view.confirm_tavern_entered()
	_check(tavern_enters == 1 and view._tavern_mode and not view._commodity_region.visible, "confirmed StarDock tavern access did not switch the port workflow mode")
	view._back_button.pressed.emit()
	_check(tavern_exits == 1 and not view._tavern_mode and view._commodity_region.visible, "leaving the tavern did not restore the port screen")
	view._tavern_button.pressed.emit()
	_check(tavern_requests == 2 and not view._tavern_mode, "re-entering the tavern skipped the server presence check")
	view.confirm_tavern_entered()
	view.show_port({"port": {"id": 9, "name": "Helix Trade Port", "type": 0, "commodities": []}}, 17)
	_check(tavern_exits == 2 and not view._tavern_mode and not view._tavern_button.visible, "replacing the port view failed to revoke Tavern access")
	view.enter_tavern()
	_check(tavern_requests == 2 and not view._tavern_mode, "Tavern access was granted on a non-StarDock port")
	view.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot port workflow tests: 27 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
