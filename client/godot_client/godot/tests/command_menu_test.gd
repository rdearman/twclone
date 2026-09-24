extends SceneTree

const CommandMenu = preload("res://CommandMenu.gd")

var failures: Array[String] = []
var captured: Array = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var menu = CommandMenu.new()
	root.add_child(menu)
	await process_frame
	menu.command_requested.connect(func(command: String, data: Dictionary, label: String, mutating: bool) -> void:
		captured.append({"command": command, "data": data, "label": label, "mutating": mutating})
	)
	menu._snapshot = {"authenticated": true, "disconnected": false, "hud": {"sector_id": 17}}
	menu._selection = {"kind": "port", "name": "Helix Trade Port", "data": {"port_id": 55}}
	menu._active_category = "PORT"
	menu._choose_action({"label": "Request commodity quote", "command": "trade.quote", "context_id": "port_id", "fields": [["commodity", "text", "Commodity", "ORE"], ["quantity", "integer", "Quantity", "2"]]})
	(menu._form_fields["commodity"] as LineEdit).text = "ORE"
	(menu._form_fields["quantity"] as LineEdit).text = "2"
	menu._submit_form()
	_check(captured.size() == 1 and captured[0]["command"] == "trade.quote", "selected-port quote action did not emit")
	if not captured.is_empty():
		_check(captured[0]["data"] == {"port_id": 55, "commodity": "ORE", "quantity": 2}, "quote request omitted authoritative port or commodity fields")
	menu._selection = {"kind": "ship", "name": "Unidentified vessel", "data": {"ship_id": 61}}
	menu._choose_action({"label": "Claim ship", "command": "ship.claim", "context_id": "ship_id", "mutating": true})
	menu._submit_form()
	_check(captured.size() == 2 and captured[1]["command"] == "ship.claim", "claim action did not emit for selected ship")
	if captured.size() > 1:
		_check(captured[1]["data"] == {"ship_id": 61} and captured[1]["mutating"], "claim action did not use selected ship identity")
	menu._choose_action({"label": "Tow ship", "command": "ship.tow", "context_id": "target_ship_id", "mutating": true})
	menu._submit_form()
	_check(captured.size() == 3 and captured[2]["data"] == {"target_ship_id": 61}, "tow action did not bind selected ship identity")
	menu._selection = {"kind": "planet", "name": "Nereid", "data": {"planet_id": 19}}
	menu._choose_action({"label": "Planet information", "command": "planet.info", "context_id": "planet_id"})
	_check(captured.size() == 4, "planet information did not emit a command")
	if captured.size() > 3:
		_check(captured[3]["command"] == "planet.info", "planet command name changed")
		_check(captured[3]["data"] == {"planet_id": 19}, "planet ID was not bound from authoritative selection")
	menu._choose_action({"command": "hardware.buy", "label": "Buy ship hardware", "mutating": true, "idempotency": true, "fields": [["code", "text", "Hardware item code", ""], ["quantity", "integer", "Quantity", "1"]]})
	menu._form_fields["code"].text = "MARKER_BEACON"
	menu._form_fields["quantity"].text = "2"
	menu._submit_form()
	_check(captured.size() == 5, "hardware purchase form did not emit")
	if captured.size() > 4:
		_check(captured[4]["command"] == "hardware.buy", "hardware purchase command changed")
		_check(captured[4]["data"].get("code") == "MARKER_BEACON" and captured[4]["data"].get("quantity") == 2, "hardware request does not match the server schema")
		_check(not str(captured[4]["data"].get("idempotency_key", "")).is_empty(), "hardware purchase omitted its idempotency key")
	menu._choose_action({"command": "mail.read", "label": "Read mail by ID", "mutating": true, "duplicate_mail_read_id": true, "fields": [["mail_id", "integer", "Mail ID", ""]]})
	(menu._form_fields["mail_id"] as LineEdit).text = "52"
	menu._submit_form()
	_check(captured.size() == 6, "mail read form did not emit")
	if captured.size() > 5:
		_check(captured[5]["data"] == {"mail_id": 52, "id": 52}, "mail read request did not bridge the schema/handler ID mismatch")
	menu._choose_action({"command": "mail.inbox", "label": "Mail inbox", "fixed": {}})
	_check(captured.size() == 7 and captured[6]["data"].is_empty(), "mail inbox sent unsupported fields")
	menu.show_shipyard({"available": [
		{"name": "Scout", "type_id": 7, "eligible": true, "net_cost": "100"},
		{"name": "Capital", "type_id": 8, "eligible": false, "reasons": ["insufficient holds"]},
	]}, "Bit Banger")
	_check(menu._shipyard_rows.get_child_count() == 2, "shipyard list did not render available hull choices")
	if menu._shipyard_rows.get_child_count() == 2:
		_check((menu._shipyard_rows.get_child(0) as Button).disabled == false, "eligible shipyard hull was disabled")
		_check((menu._shipyard_rows.get_child(1) as Button).disabled, "ineligible shipyard hull was selectable")
	menu._choose_shipyard_hull(7, "Bit Banger")
	menu._shipyard_name_input.text = "New Scout"
	menu._submit_shipyard_upgrade()
	_check(captured.size() == 8 and captured[7]["command"] == "shipyard.upgrade", "shipyard selection did not submit upgrade")
	if captured.size() > 7:
		_check(captured[7]["data"] == {"new_type_id": 7, "new_ship_name": "New Scout"} and captured[7]["mutating"], "shipyard upgrade payload was incorrect")
	menu._choose_action({"command": "player.set_prefs", "label": "Set clock preference", "mutating": true, "preference_key": "ui.clock_24h", "preference_type": "bool", "fields": [["value", "boolean", "Use 24-hour clock", "false"]]})
	(menu._form_fields["value"] as CheckBox).button_pressed = false
	menu._submit_form()
	_check(captured.size() == 9 and captured[8]["data"] == {"items": [{"key": "ui.clock_24h", "type": "bool", "value": false}]}, "preference form did not construct the server's items payload")
	menu.show_deployed_assets({"entries": [{"asset_id": 21, "count": 12, "offense_mode": "DEFEND"}]}, "fighters", 17)
	await process_frame
	_check(menu._shipyard_rows.get_child_count() == 1, "deployed fighter asset was not rendered")
	menu._recall_asset(21)
	_check(captured.size() == 10 and captured[9]["command"] == "fighters.recall", "fighter recall did not emit the server command")
	if captured.size() > 9:
		_check(captured[9]["data"] == {"sector_id": 17, "asset_id": 21} and captured[9]["mutating"], "fighter recall omitted sector or asset identity")
	_check(not menu._categories().has("TAVERN"), "Tavern commands were exposed outside the StarDock tavern")
	menu._choose_action({"command": "tavern.dice.play", "label": "Play dice", "mutating": true})
	_check(captured.size() == 10, "a Tavern RPC was sent without Tavern access")
	menu.set_tavern_access(true)
	_check(menu._categories().has("TAVERN") and menu._active_category == "TAVERN", "entering the Tavern did not expose its command category")
	menu._choose_action({"label": "Post tavern graffiti", "command": "tavern.graffiti.post", "mutating": true, "fields": [["text", "text", "Graffiti", ""]]})
	(menu._form_fields["text"] as LineEdit).text = "Hello, spacefarers"
	menu._submit_form()
	_check(captured.size() == 11 and captured[10]["data"] == {"text": "Hello, spacefarers"}, "tavern graffiti did not send its handler-confirmed text field")
	menu._choose_action({"label": "Buy a rumour hint · 50 CR", "command": "tavern.rumour.get_hint", "mutating": true})
	menu._submit_form()
	_check(captured.size() == 12 and captured[11]["command"] == "tavern.rumour.get_hint" and captured[11]["data"].is_empty(), "paid rumour action did not confirm and send the empty server payload")
	menu.set_tavern_access(false)
	_check(not menu._categories().has("TAVERN") and menu._active_category != "TAVERN", "leaving the Tavern left its commands exposed")
	menu.set_activity_history(["Maintenance · Scheduled in ten minutes."])
	menu._choose_action({"local_activity": true})
	_check(captured.size() == 12, "opening recent event history emitted a server command")
	_check(menu._result_text.text == "Maintenance · Scheduled in ten minutes.", "recent event history did not show the sanitized event summary")
	menu._choose_action({"local_help": true})
	_check(captured.size() == 12, "opening the field guide emitted a server command")
	_check(menu._result_text.text.contains("Actions are discrete requests") and menu._result_text.text.contains("screen position of a marker is decorative"), "field guide omitted the authoritative command and decorative-coordinate rules")
	menu._choose_action({"label": "Jettison cargo", "command": "trade.jettison", "mutating": true, "fields": [["commodity", "text", "Commodity", "ore"], ["quantity", "integer", "Quantity", "1"]]})
	menu._form_fields["commodity"].text = "ore"
	menu._form_fields["quantity"].text = "3"
	menu._submit_form()
	_check(captured.size() == 13 and captured[12]["command"] == "trade.jettison", "handler-compatible cargo jettison command was not submitted")
	if captured.size() > 12:
		_check(captured[12]["data"] == {"commodity": "ore", "quantity": 3} and captured[12]["mutating"], "cargo jettison did not use the implemented commodity/quantity contract")
	var display := menu._format_result({"credits": 0, "session_token": "secret", "cargo": [{"commodity": "ORE", "quantity": 1}]})
	_check(display.contains("Credits: 0"), "legitimate zero was not displayed")
	_check(not display.contains("secret") and not display.contains("session token"), "sensitive result data entered player output")
	menu.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot command menu tests: 39 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
