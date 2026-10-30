extends SceneTree

const CommandMenu = preload("res://CommandMenu.gd")

var failures: Array[String] = []
var captured: Array = []
var checks := 0

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
	menu._active_category = "COMPUTER"
	menu._render_actions()
	var shipyard_action_button := _action_button(menu, "Shipyard catalogue")
	_check(shipyard_action_button != null and shipyard_action_button.disabled and shipyard_action_button.tooltip_text.contains("Dock at a supported shipyard"), "shipyard action was enabled without confirmed docked shipyard state")
	menu._snapshot["ship"] = {"ported": 55}
	menu._snapshot["sector"] = {"ports": [{"id": 55, "type": 9}]}
	menu._render_actions()
	await process_frame
	shipyard_action_button = _action_button(menu, "Shipyard catalogue")
	_check(shipyard_action_button != null and not shipyard_action_button.disabled, "shipyard action stayed disabled for a confirmed docked StarDock")
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
	menu._choose_action({"command": "hardware.buy", "label": "Buy ship hardware", "mutating": true, "idempotency": true, "fields": [["item_code", "text", "Hardware item code", ""], ["quantity", "integer", "Quantity", "1"]]})
	menu._form_fields["item_code"].text = "MARKER_BEACON"
	menu._form_fields["quantity"].text = "2"
	menu._submit_form()
	_check(captured.size() == 5, "hardware purchase form did not emit")
	if captured.size() > 4:
		_check(captured[4]["command"] == "hardware.buy", "hardware purchase command changed")
		_check(captured[4]["data"].get("item_code") == "MARKER_BEACON" and captured[4]["data"].get("quantity") == 2, "hardware request does not match the server schema")
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
	menu._choose_action({"label": "Jettison cargo", "command": "trade.jettison", "mutating": true, "form_notice": "Permanently destroys cargo and consumes one turn.", "fields": [["commodity", "text", "Commodity", "ore"], ["quantity", "integer", "Quantity", "1"]]})
	var jettison_notice_shown := false
	for form_child in menu._form_content.get_children():
		if form_child is Label and (form_child as Label).text.contains("consumes one turn"):
			jettison_notice_shown = true
	_check(jettison_notice_shown, "jettison form did not explain cargo loss and turn cost")
	menu._form_fields["commodity"].text = "ore"
	menu._form_fields["quantity"].text = "3"
	menu._submit_form()
	_check(captured.size() == 13 and captured[12]["command"] == "trade.jettison", "handler-compatible cargo jettison command was not submitted")
	if captured.size() > 12:
		_check(captured[12]["data"] == {"commodity": "ore", "quantity": 3} and captured[12]["mutating"], "cargo jettison did not use the implemented commodity/quantity contract")
	menu._snapshot["freshness"] = {"sector": "fresh"}
	var navigation_actions: Array = CommandMenu.COMMANDS.get("NAVIGATION", [])
	var route_action: Dictionary = {}
	for candidate in navigation_actions:
		if str(candidate.get("command", "")) == "move.autopilot.start":
			route_action = candidate
	_check(not route_action.is_empty(), "navigation menu does not expose the implemented route planner")
	menu._choose_action(route_action)
	(menu._form_fields["to_sector_id"] as LineEdit).text = "42"
	menu._submit_form()
	_check(captured.size() == 14 and captured[13]["command"] == "move.autopilot.start", "route planner did not use the implemented compatible command")
	if captured.size() > 13:
		_check(captured[13]["data"] == {"to_sector_id": 42} and captured[13]["mutating"], "route planner did not mark the saved-route update as mutating")
	var autopilot_status_action: Dictionary = {}
	for candidate in navigation_actions:
		if str(candidate.get("command", "")) == "move.autopilot.status":
			autopilot_status_action = candidate
	_check(not autopilot_status_action.is_empty(), "navigation menu does not expose the implemented autopilot status query")
	menu._choose_action(autopilot_status_action)
	_check(captured.size() == 15 and captured[14]["command"] == "move.autopilot.status" and captured[14]["data"].is_empty() and not captured[14]["mutating"], "autopilot status did not issue the empty read-only command")
	var expected_controls := [
		{"label": "Autopilot · stop at next sector", "command": "move.autopilot.control", "data": {"action": "stop_at_next"}},
		{"label": "Autopilot · continue route", "command": "move.autopilot.control", "data": {"action": "continue"}},
		{"label": "Autopilot · express mode", "command": "move.autopilot.control", "data": {"action": "express"}},
		{"label": "Autopilot · stop saved route now", "command": "move.autopilot.stop", "data": {}},
	]
	for expected in expected_controls:
		var control_action: Dictionary = {}
		for candidate in navigation_actions:
			if str(candidate.get("label", "")) == str(expected["label"]):
				control_action = candidate
		_check(not control_action.is_empty(), "%s is not exposed in navigation controls" % str(expected["label"]))
		if control_action.is_empty():
			continue
		menu._choose_action(control_action)
		_check(menu._form_dialog.visible, "%s did not confirm its state-changing server request" % str(expected["label"]))
		menu._form_dialog.confirmed.emit()
		var sent: Dictionary = captured.back() if not captured.is_empty() else {}
		_check(sent.get("command") == expected["command"] and sent.get("data") == expected["data"] and sent.get("mutating", false), "%s sent a payload outside the documented autopilot contract" % str(expected["label"]))
	var warp_action: Dictionary = {}
	for candidate in navigation_actions:
		if str(candidate.get("command", "")) == "move.warp":
			warp_action = candidate
	_check(not warp_action.is_empty(), "navigation menu does not expose direct sector-number warp")
	_check(not warp_action.get("fields", []).is_empty() and str(warp_action["fields"][0][0]) == "to_sector_id", "numeric warp does not request the documented destination field")
	menu._choose_action(warp_action)
	(menu._form_fields["to_sector_id"] as LineEdit).text = "18"
	menu._submit_form()
	_check(captured.size() == 20 and captured[19]["command"] == "move.warp" and captured[19]["data"] == {"to_sector_id": 18} and captured[19]["mutating"], "numeric warp did not use the direct authoritative move command")
	menu._snapshot["disconnected"] = false
	var session_commands: Array = CommandMenu.COMMANDS.get("SESSION", [])
	_check(session_commands.size() == 1 and session_commands[0].get("local_logout", false), "session category does not expose confirmed logout")
	menu._choose_action(session_commands[0])
	_check(menu._logout_dialog.visible, "logout did not require a confirmation step")
	menu._logout_dialog.confirmed.emit()
	_check(captured.size() == 21 and captured[20]["command"] == "auth.logout" and captured[20]["data"].is_empty(), "confirmed logout did not send the documented empty payload")
	var pinned_help := {"label": "Controls and command flow", "local_help": true}
	menu._pinned_actions.clear()
	menu._key_bindings.clear()
	menu._toggle_pin(pinned_help)
	_check(menu._categories().has("PINNED"), "pinning a command did not add the PINNED category")
	var help_key := menu._action_storage_key(pinned_help)
	menu._begin_shortcut_binding(help_key)
	var modified_key := InputEventKey.new()
	modified_key.pressed = true
	modified_key.keycode = KEY_K
	modified_key.ctrl_pressed = true
	_check(menu.capture_shortcut(modified_key) and menu._awaiting_binding_command == help_key and not menu._key_bindings.has(help_key), "shortcut binding accepted a modifier combination")
	var plain_key := InputEventKey.new()
	plain_key.pressed = true
	plain_key.keycode = KEY_H
	_check(menu.capture_shortcut(plain_key) and int(menu._key_bindings.get(help_key, 0)) == KEY_H, "pinned command did not accept a single-key binding")
	_check(menu.dispatch_shortcut(KEY_H) and menu._result_text.text.contains("PINNED category"), "pinned keyboard shortcut did not run its local command")
	menu._choose_action(pinned_help)
	_check(menu._result_text.text.contains("Repair ship restores hull") and menu._result_text.text.contains("News feed shows published game headlines"), "field guide omitted ship service and news guidance")
	menu._toggle_pin(pinned_help)
	_check(not menu._categories().has("PINNED"), "unpinned command left an empty PINNED category")
	menu._selection.clear()
	menu._active_category = "COMPUTER"
	menu._render_actions()
	var computer_help_button := _action_button(menu, "Help · controls and command flow")
	_check(computer_help_button != null, "Computer menu omitted the direct Help shortcut")
	if computer_help_button != null:
		computer_help_button.pressed.emit()
		_check(menu._result_text.text.contains("COMMANDS AND SHORTCUTS"), "Computer Help shortcut did not open the field guide")
	var rename_button := _action_button(menu, "Rename ship · unavailable")
	_check(rename_button != null and rename_button.disabled and rename_button.tooltip_text.contains("ship_id and new_name"), "unsupported ship rename was not explained as disabled")
	menu._active_category = "COMMUNICATIONS"
	menu._render_actions()
	var private_chat_button := _action_button(menu, "Private message · unavailable")
	_check(private_chat_button != null and private_chat_button.disabled, "unsupported private messaging was not visibly disabled")
	menu._active_category = "MAIL & SUBSCRIPTIONS"
	menu._render_actions()
	var send_mail_button := _action_button(menu, "Send mail · unavailable")
	var delete_mail_button := _action_button(menu, "Delete mail · unavailable")
	_check(send_mail_button != null and send_mail_button.disabled and delete_mail_button != null and delete_mail_button.disabled, "mail contract gaps were not shown as disabled actions")
	var add_subscription_button := _action_button(menu, "Add subscription")
	var remove_subscription_button := _action_button(menu, "Remove subscription")
	_check(add_subscription_button != null and not add_subscription_button.disabled and remove_subscription_button != null and not remove_subscription_button.disabled, "documented subscription actions were disabled")
	menu._active_category = "NEWS & RECORDS"
	menu._render_actions()
	var insurance_buy_button := _action_button(menu, "Buy insurance · server stub")
	_check(insurance_buy_button != null and insurance_buy_button.disabled, "stub insurance operation was presented as usable")
	var count_before_unavailable := captured.size()
	menu._choose_action({"label": "Private message", "unavailable_reason": "chat.send contract mismatch"})
	_check(captured.size() == count_before_unavailable, "unavailable action emitted a server command")
	menu._choose_action({"label": "Add subscription", "unavailable_reason": "subscribe contract mismatch"})
	_check(captured.size() == count_before_unavailable, "unavailable subscription emitted a server command")
	var routes_action: Dictionary = {}
	for candidate in CommandMenu.COMMANDS.get("COMPUTER", []):
		if str(candidate.get("command", "")) == "player.computer.recommend_routes":
			routes_action = candidate
	menu._choose_action(routes_action)
	(menu._form_fields["max_hops_between"] as LineEdit).text = "7"
	(menu._form_fields["limit"] as LineEdit).text = "4"
	menu._submit_form()
	_check(captured.size() == count_before_unavailable + 1 and captured.back()["data"] == {"max_hops_between": 7, "require_two_way": 0, "limit": 4}, "route recommendation request omitted or misformatted its optional two-way filter")
	var display := menu._format_result({"credits": 0, "session_token": "secret", "cargo": [{"commodity": "ORE", "quantity": 1}]})
	_check(display.contains("Credits: 0"), "legitimate zero was not displayed")
	_check(not display.contains("secret") and not display.contains("session token"), "sensitive result data entered player output")
	var news_text := menu.format_result("News feed", {"articles": [{"headline": "Frontier Trade", "category": "market", "timestamp": "2026-09-27", "body": "Ports report increased demand."}]})
	_check(news_text.contains("Frontier Trade · market · 2026-09-27") and news_text.contains("Ports report increased demand."), "news feed was not rendered as readable article content")
	_check(menu.format_result("News feed", {"articles": []}) == "No news articles are available.", "empty news response was not explained clearly")
	var notice_text := menu.format_result("Notices", {"items": [{"title": "Beacon alert", "body": "Traffic ahead", "scope": "sector", "seen_at": null}]})
	_check(notice_text.contains("[NEW] Beacon alert · sector") and notice_text.contains("Traffic ahead"), "notice list was not rendered with read state and message")
	_check(menu.format_result("Notices", {"items": []}) == "No current notices.", "empty notice response was not explained clearly")
	var bounty_action: Dictionary = {}
	for candidate in CommandMenu.COMMANDS.get("COMMUNICATIONS", []):
		if str(candidate.get("command", "")) == "bounty.list":
			bounty_action = candidate
	_check(not bounty_action.is_empty() and bounty_action.get("fixed", {}) == {}, "read-only bounty board was not exposed through communications")
	var bounty_text := menu.format_result("Active bounties", {"bounties": [{"bounty_id": 19, "target_id": 7, "target_name": "Ranger", "reward": 2500}]})
	_check(bounty_text.contains("#19 · Ranger · 2500 CR"), "authoritative bounty row was not presented")
	_check(menu.format_result("Active bounties", {"bounties": []}) == "There are no active bounties.", "empty bounty response was not explained")
	var police_status_action: Dictionary = {}
	var police_bribe_action: Dictionary = {}
	var police_surrender_action: Dictionary = {}
	for candidate in CommandMenu.COMMANDS.get("COMPUTER", []):
		match str(candidate.get("command", "")):
			"police.status": police_status_action = candidate
			"police.bribe": police_bribe_action = candidate
			"police.surrender": police_surrender_action = candidate
	_check(not police_status_action.is_empty() and police_status_action.get("fixed", {}) == {}, "current police status was not exposed as a read-only current-context query")
	_check(police_bribe_action.get("mutating", false) and police_bribe_action.get("fields", [])[0][0] == "amount", "bribe control did not require the player's amount and mark the command as mutating")
	_check(police_surrender_action.get("mutating", false) and police_surrender_action.get("fixed", {}) == {}, "surrender control was not marked for confirmation")
	var police_status_text := menu.format_result("Police status", {"cluster_id": 4, "suspicion": 2, "wanted_level": 1, "banned": false})
	_check(police_status_text.contains("Suspicion: 2") and police_status_text.contains("Banned: false"), "police status did not show authoritative incident values")
	var police_outcome := menu.format_result("Offer a bribe", {"data": {"message": "Bribe accepted.", "suspicion_after": 0, "wanted_level_after": 0}})
	_check(police_outcome.contains("Bribe accepted.") and police_outcome.contains("Wanted level: 0"), "police outcome did not show the server result")
	var trade_sell_action: Dictionary = {}
	var trade_accept_action: Dictionary = {}
	for candidate in CommandMenu.COMMANDS.get("COMMUNICATIONS", []):
		if str(candidate.get("label", "")) == "Create sell offer":
			trade_sell_action = candidate
		if str(candidate.get("command", "")) == "trade.accept":
			trade_accept_action = candidate
	_check(trade_sell_action.get("command", "") == "trade.offer" and trade_sell_action.get("fixed", {}).get("mode", "") == "sell" and trade_sell_action.get("idempotency", false), "sell offer does not use the documented request and retry key")
	menu._choose_action(trade_sell_action)
	menu._form_fields["to_player_id"].text = "8"
	menu._form_fields["commodity"].text = "ORE"
	menu._form_fields["quantity"].text = "2"
	menu._form_fields["price"].text = "0"
	var offer_count := captured.size()
	menu._submit_form()
	_check(captured.size() == offer_count + 1 and captured.back()["command"] == "trade.offer", "player sell offer did not submit")
	if captured.size() > offer_count:
		_check(captured.back()["data"].get("mode") == "sell" and captured.back()["data"].get("price") == 0 and str(captured.back()["data"].get("idempotency_key", "")).length() > 0, "sell offer fields or idempotency key were not sent canonically")
	_check(trade_accept_action.get("mutating", false) and trade_accept_action.get("fields", [])[0][0] == "offer_id", "offer acceptance did not require a server-issued offer ID")
	var notice_ack_action: Dictionary = {}
	for candidate in CommandMenu.COMMANDS.get("COMMUNICATIONS", []):
		if str(candidate.get("command", "")) == "notice.ack":
			notice_ack_action = candidate
	_check(notice_ack_action.get("mutating", false) and not notice_ack_action.has("fixed"), "notice acknowledgement did not require a specific server-listed notice ID")
	var notice_ack_count := captured.size()
	menu._choose_action(notice_ack_action)
	if not notice_ack_action.is_empty():
		menu._form_fields["notice_id"].text = "72"
		menu._submit_form()
	_check(captured.size() == notice_ack_count + 1 and captured.back()["command"] == "notice.ack" and captured.back()["data"] == {"notice_id": 72} and captured.back()["mutating"], "notice acknowledgement did not send the selected notice ID as a mutation")
	var repair_action: Dictionary = {}
	for candidate in CommandMenu.COMMANDS.get("COMPUTER", []):
		if str(candidate.get("command", "")) == "ship.repair":
			repair_action = candidate
	_check(not repair_action.is_empty(), "ship computer does not expose ship repair")
	menu._snapshot = {"authenticated": true, "disconnected": false, "hud": {"sector_id": 17}, "ship": {"ported": 0}, "sector": {"ports": [{"id": 55, "type": 9}]}}
	menu._selection.clear()
	menu._active_category = "COMPUTER"
	menu._render_actions()
	await process_frame
	var repair_button := _action_button(menu, "Repair ship at shipyard")
	_check(repair_button != null and repair_button.disabled, "ship repair was available without a shipyard dock")
	menu._snapshot["ship"] = {"ported": 55}
	menu._render_actions()
	await process_frame
	repair_button = _action_button(menu, "Repair ship at shipyard")
	_check(repair_button != null and not repair_button.disabled, "ship repair stayed unavailable at a confirmed shipyard")
	var before_repair := captured.size()
	menu._choose_action(repair_action)
	_check(menu._form_dialog.visible, "ship repair did not present its explicit action confirmation")
	menu._submit_form()
	_check(captured.size() == before_repair + 1 and captured.back()["command"] == "ship.repair" and captured.back()["data"].is_empty() and captured.back()["mutating"], "ship repair did not send the server-supported empty request")
	var password_action: Dictionary = {}
	for candidate in CommandMenu.COMMANDS.get("SETTINGS & NOTES", []):
		if str(candidate.get("command", "")) == "auth.change_password":
			password_action = candidate
	_check(not password_action.is_empty(), "settings do not expose the server-supported password change")
	menu._choose_action(password_action)
	_check((menu._form_fields["old_password"] as LineEdit).secret and (menu._form_fields["new_password"] as LineEdit).secret and (menu._form_fields["confirm_password"] as LineEdit).secret, "password fields are not masked")
	(menu._form_fields["old_password"] as LineEdit).text = "qa-old-ephemeral"
	(menu._form_fields["new_password"] as LineEdit).text = "qa-new-ephemeral-a"
	(menu._form_fields["confirm_password"] as LineEdit).text = "qa-new-ephemeral-b"
	var before_password_change := captured.size()
	menu._submit_form()
	_check(captured.size() == before_password_change, "mismatched password confirmation reached the server")
	(menu._form_fields["confirm_password"] as LineEdit).text = "qa-new-ephemeral-a"
	menu._submit_form()
	_check(captured.size() == before_password_change + 1, "matching password confirmation did not submit")
	if captured.size() > before_password_change:
		var password_request: Dictionary = captured.back()
		_check(password_request["command"] == "auth.change_password" and password_request["mutating"], "password change used the wrong command or was not marked mutating")
		_check(password_request["data"].keys().size() == 2 and password_request["data"].has("old_password") and password_request["data"].has("new_password"), "password confirmation leaked into or altered the published request fields")
		_check(not password_request["data"].has("confirm_password") and (menu._form_fields["old_password"] as LineEdit).text.is_empty() and (menu._form_fields["new_password"] as LineEdit).text.is_empty(), "password values remained in the form after submission")
	var before_corporation_actions := captured.size()
	menu._choose_action({"label": "Member roster", "command": "corp.roster", "context_player_corp_id": true})
	_check(captured.size() == before_corporation_actions, "corporation roster guessed a missing corporation ID")
	menu._snapshot["player"] = {"corp_id": 10}
	menu._snapshot["freshness"] = {"sector": "fresh"}
	menu._choose_action({"label": "Member roster", "command": "corp.roster", "context_player_corp_id": true})
	_check(captured.size() == before_corporation_actions + 1 and captured.back()["command"] == "corp.roster", "corporation roster action did not emit")
	if captured.size() > before_corporation_actions:
		_check(captured.back()["data"] == {"corp_id": 10}, "corporation roster did not use confirmed player corp_id")
	menu._active_category = "PLANET"
	menu._selection = {"kind": "planet", "name": "Nereid", "data": {"planet_id": 19}}
	menu._snapshot["hud"]["sector_id"] = 17
	menu._render_actions()
	var attack_planet_button := _action_button(menu, "Attack this planet")
	_check(attack_planet_button != null and not attack_planet_button.disabled, "listed non-FedSpace planet attack was not available")
	menu._choose_action({"label": "Attack this planet", "command": "combat.attack_planet", "context_id": "planet_id", "mutating": true})
	menu._submit_form()
	_check(captured.size() == before_corporation_actions + 2 and captured.back()["data"] == {"planet_id": 19}, "planet attack did not use the selected authoritative planet ID")
	menu._snapshot["hud"]["sector_id"] = 5
	menu._render_actions()
	await process_frame
	attack_planet_button = _action_button(menu, "Attack this planet")
	_check(attack_planet_button != null and attack_planet_button.disabled and attack_planet_button.tooltip_text.contains("FedSpace"), "planet attack remained enabled in FedSpace")
	menu._snapshot["hud"]["sector_id"] = 17
	menu._selection["data"] = {"planet_id": 1}
	menu._render_actions()
	await process_frame
	attack_planet_button = _action_button(menu, "Attack this planet")
	_check(attack_planet_button != null and attack_planet_button.disabled and attack_planet_button.tooltip_text.contains("Terra"), "planet attack remained enabled against protected Terra")
	menu._active_category = "COMPUTER"
	menu._choose_action({"label": "Ferengi traders and open offers", "command": "ferengi.traders", "fixed": {}})
	_check(captured.size() == before_corporation_actions + 3 and captured.back()["command"] == "ferengi.traders" and captured.back()["data"].is_empty(), "Ferengi trader listing did not use the published current-sector read command")
	menu._choose_action({"label": "Accept Ferengi offer", "command": "ferengi.deal.accept", "fields": [["deal_id", "integer", "Deal ID", ""]], "mutating": true})
	(menu._form_fields["deal_id"] as LineEdit).text = "91"
	menu._submit_form()
	_check(captured.size() == before_corporation_actions + 4 and captured.back()["command"] == "ferengi.deal.accept" and captured.back()["data"] == {"deal_id": 91} and captured.back()["mutating"], "Ferengi acceptance did not send the entered deal ID")
	menu._choose_action({"label": "Reject Ferengi offer", "command": "ferengi.deal.reject", "fields": [["deal_id", "integer", "Deal ID", ""]], "mutating": true})
	(menu._form_fields["deal_id"] as LineEdit).text = "92"
	menu._submit_form()
	_check(captured.size() == before_corporation_actions + 5 and captured.back()["command"] == "ferengi.deal.reject" and captured.back()["data"] == {"deal_id": 92} and captured.back()["mutating"], "Ferengi rejection did not send the entered deal ID")
	menu._choose_action({"label": "Create corporation", "command": "corp.create", "mutating": true, "fields": [["name", "text", "Corporation name", ""]]})
	(menu._form_fields["name"] as LineEdit).text = "Nova"
	menu._submit_form()
	_check(captured.size() == before_corporation_actions + 6 and captured.back()["data"] == {"name": "Nova"}, "corporation create included fields outside the published request schema")
	menu._active_category = "MAIL & SUBSCRIPTIONS"
	menu._render_actions()
	var add_subscription_button_again := _action_button(menu, "Add subscription")
	var remove_subscription_button_again := _action_button(menu, "Remove subscription")
	_check(add_subscription_button_again != null and not add_subscription_button_again.disabled, "documented subscription add remained unavailable")
	_check(remove_subscription_button_again != null and not remove_subscription_button_again.disabled, "documented subscription removal remained unavailable")
	menu._choose_action({"label": "Add subscription", "command": "subscribe.add", "fields": [["topic", "text", "Topic pattern", ""]], "mutating": true})
	(menu._form_fields["topic"] as LineEdit).text = "sector.*"
	menu._submit_form()
	_check(captured.back().get("command") == "subscribe.add" and captured.back().get("data") == {"topic": "sector.*"}, "subscription add did not send the canonical topic field")
	menu._choose_action({"label": "Remove subscription", "command": "subscribe.remove", "fields": [["topic", "text", "Topic", ""]], "mutating": true})
	(menu._form_fields["topic"] as LineEdit).text = "sector.42"
	menu._submit_form()
	_check(captured.back().get("command") == "subscribe.remove" and captured.back().get("data") == {"topic": "sector.42"}, "subscription removal did not send the canonical topic field")
	var unsupported_ship_commands := ["ship.sell", "ship.transfer", "ship.transfer_cargo", "ship.upgrade", "shipyard.sell"]
	for actions in CommandMenu.COMMANDS.values():
		for action in actions:
			_check(not unsupported_ship_commands.has(str(action.get("command", ""))), "command menu exposed an unimplemented ship command")
	menu._selection = {"kind": "ship", "name": "Unidentified vessel", "data": {"ship_id": 61}}
	menu._active_category = "SHIP"
	menu._render_actions()
	var inspect_action_button := _action_button(menu, "Inspect ship details")
	_check(inspect_action_button != null and not inspect_action_button.disabled, "selected ship inspection stayed unavailable despite the server contract")
	var before_inspect := captured.size()
	menu._choose_action({"label": "Inspect ship details", "command": "ship.inspect", "context_id": "ship_id"})
	_check(captured.size() == before_inspect + 1 and captured.back()["command"] == "ship.inspect" and captured.back()["data"] == {"ship_id": 61}, "ship inspection did not bind the selected ship ID")
	menu.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot command menu tests: %d checks passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)

func _action_button(menu, label: String) -> Button:
	for child in menu._action_list.get_children():
		var found := _find_button(child, label)
		if found != null:
			return found
	return null

func _find_button(node: Node, label: String) -> Button:
	if node is Button and (node as Button).text == label:
		return node as Button
	for child in node.get_children():
		var found := _find_button(child, label)
		if found != null:
			return found
	return null
