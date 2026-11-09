extends SceneTree

const GameplayView = preload("res://GameplayView.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var view = GameplayView.new()
	view.size = Vector2(1400, 900)
	root.add_child(view)
	await process_frame
	var commands = view.command_menu
	var trades: Array[Dictionary] = []
	view.trade_requested.connect(func(direction: String, port_id: int, sector_id: int, commodity: String, quantity: int, account: int) -> void:
		trades.append({"direction": direction, "port_id": port_id, "sector_id": sector_id, "commodity": commodity, "quantity": quantity, "account": account})
	)
	view.port_workflow.trade_requested.emit("buy", 7, 17, "ORE", 3, 0)
	view.port_workflow.trade_requested.emit("sell", 7, 17, "ORG", 2, 1)
	_check(trades == [{"direction": "buy", "port_id": 7, "sector_id": 17, "commodity": "ORE", "quantity": 3, "account": 0}, {"direction": "sell", "port_id": 7, "sector_id": 17, "commodity": "ORG", "quantity": 2, "account": 1}], "port trade signal lost its account or trade context")
	view.confirm_trade_quote("buy", "ORE", 3, 1, {"total_buy_price": 120, "buy_price": 40})
	_check(view.port_workflow._quoted_prices.get("ORE", {}).get("buy", null) == 40, "authoritative quote price was not returned to the port row")
	view.dismiss_trade_quote()
	_check(not view.trade_quote_dialog.visible, "ordinary trade unexpectedly opened a confirmation dialog")
	_check(commands is PanelContainer, "general commands are not a docked panel")
	_check(commands.get_parent() == view.workspace_stack, "command panel is not placed in the viewport command column")
	_check(view.primary_tabs.tab_count == 5, "primary gameplay workspaces were not created")
	view._on_workspace_tab_changed(1)
	_check(view._active_workspace == "NAVIGATION" and commands._active_category == "NAVIGATION", "Navigation workspace did not select the existing navigation command category")
	view._on_workspace_tab_changed(0)
	_check(view._active_workspace == "SECTOR" and not commands.visible, "Sector workspace did not prioritise the graphical sector view")
	_check(view.center_split.visible and view.workspace_stack.get_parent() == view.center_split, "Sector workspace did not preserve the two-column viewport layout")
	_check(is_equal_approx(view.toast_panel.anchor_left, 0.5) and is_equal_approx(view.toast_panel.anchor_right, 0.5), "notification ribbon is not centered at the bottom")
	view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": 17, "hull": 73},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": {"id": 17, "adjacent_sector_ids": [18], "ports": [], "planets": [], "ships": []},
	})
	_check(view.hull_label.text == "HULL  73", "authoritative ship hull was not visible in the persistent HUD")
	_check(view.credits_label.text == "— CR" and view.turns_label.text == "— TURNS", "missing authoritative HUD values did not remain unavailable")
	view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": 44, "credits": 42850, "turns_remaining": 318, "cargo_used": 24, "cargo_total": 50, "shields": 80, "hull": 43, "fighters": 120},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": {"id": 44, "adjacent_sector_ids": [45, 91], "ports": [], "planets": [], "ships": []}
	})
	_check(view.credits_label.text == "42,850 CR", "credits HUD did not reflect the supplied authoritative state")
	_check(view.turns_label.text == "318 TURNS", "turn HUD did not reflect the supplied authoritative state")
	_check(view.cargo_label.text == "CARGO  24/50", "cargo HUD did not reflect the supplied authoritative state")
	_check(view.shields_label.text == "SHIELDS  80", "shield HUD did not reflect the supplied authoritative state")
	_check(view.hull_label.text == "HULL  43", "hull HUD did not reflect the supplied authoritative state")
	_check(view.fighters_label.text == "FIGHTERS  120", "fighter HUD did not reflect the supplied authoritative state")
	_check(view.sector_title.text.contains("44") and view.warp_links_label.text == "WARP LINKS: 45 · 91", "sector and warp-link HUD did not reflect the supplied authoritative state")
	view.show_command_result("Sector density scan", {"sectors": [21, 21.0, 213, 100.0, 808, 0, 1793, null]})
	_check(not view.command_menu._result_dialog.visible, "read-only result opened a modal dialog")
	_check(view.information_text.text.contains("Sector 21 · Density: 21") and view.information_text.text.contains("Sector 1793 · Density: unavailable"), "density result was not rendered in the Information panel")
	var command_calls := 0
	view.command_requested.connect(func(_command: String, _data: Dictionary, _label: String, _mutating: bool) -> void: command_calls += 1)
	view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": 17},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": {"id": 17, "ports": [{"id": 7, "name": "Batiredigo", "type": 2}], "planets": [], "ships": [], "adjacent_sector_ids": []}
	})
	var port_nodes: Dictionary = view.sector_view._sprite_nodes.get("port:7", {})
	_check(not port_nodes.is_empty(), "port artwork node was not created")
	var port_hit_area: Area2D = port_nodes["root"].get_child(1)
	_check(port_hit_area.input_pickable and port_hit_area.get_child(0).shape.radius > 0.0, "port artwork does not have a usable hit area")
	var port_click := InputEventMouseButton.new()
	port_click.button_index = MOUSE_BUTTON_LEFT
	port_click.pressed = true
	port_click.position = port_nodes["root"].get_global_transform_with_canvas().origin
	view.sector_view._input(port_click)
	_check(view.selection_title.text == "PORT" and view.selection_detail.text.contains("Batiredigo"), "artwork click did not select the exact port in Sector Contents")
	_check(not view.notification_label.visible, "artwork selection recreated the floating selection banner")
	_check(not view.command_menu._result_dialog.visible and view.information_text.text.contains("Sector 21 · Density: 21"), "port selection covered or replaced the Information panel")
	_check(command_calls == 0, "artwork selection emitted a gameplay command")
	view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": 17},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": {"id": 17, "ports": [{"name": "Unnamed-ID Port", "type": 2}], "planets": [], "ships": [], "adjacent_sector_ids": []}
	})
	_check(not view.sector_view._sprite_nodes.get("port:unknown:unnamed-id_port", {}).is_empty(), "port without an ID failed to render selectable artwork")
	var activations: Array[int] = []
	view.warp_activated.connect(func(destination: int) -> void: activations.append(destination))
	view._on_warp_selected("warp:18", 18)
	_check(activations.is_empty(), "single click moved instead of selecting the adjacent sector")
	var double_click := InputEventMouseButton.new()
	double_click.button_index = MOUSE_BUTTON_LEFT
	double_click.pressed = true
	double_click.double_click = true
	view._on_warp_row_gui_input(double_click, 18)
	_check(activations == [18], "double-click did not activate the selected warp destination")
	view.enter_port_workflow({"port": {"id": 9, "name": "Helix", "type": 9, "commodities": []}})
	_check(not commands._tavern_access, "opening StarDock port information prematurely exposed Tavern actions")
	view.port_workflow.confirm_tavern_entered()
	_check(commands._tavern_access and commands._active_category == "TAVERN" and commands.get_meta("context_label").text.contains("confirmed by the server"), "confirmed Tavern entry did not open its persistent command category and context")
	view.port_workflow._back_button.pressed.emit()
	_check(not commands._tavern_access and commands._active_category != "TAVERN", "leaving Tavern did not remove Tavern commands from the command rail")
	view.enter_port_workflow({"port": {"id": 9, "name": "Helix", "type": 9, "commodities": []}})
	view.port_workflow.confirm_tavern_entered()
	view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": 18},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": {"id": 18, "adjacent_sector_ids": [17], "ports": [], "planets": [], "ships": [{"ship_id": 74, "name": "Long Meridian", "owner_player_id": 17, "owner_name": "Renamed Captain"}]},
	})
	_check(not view.port_workflow.visible and not commands._tavern_access, "authoritative movement did not close the old port/Tavern context")
	view.select_object_key("ship:74")
	var inspect_button := _context_button(view, "INSPECT SHIP DETAILS")
	var private_message_button := _context_button(view, "PRIVATE MESSAGE OWNER")
	var attack_button := _context_button(view, "ATTACK THIS SHIP")
	var tow_button := _context_button(view, "TOW THIS SHIP")
	_check(inspect_button != null and not inspect_button.disabled, "selected ship inspection was not enabled with a confirmed ID")
	_check(private_message_button != null and not private_message_button.disabled, "authoritative owner ID did not enable private ship-owner messaging")
	if private_message_button != null:
		var before_private_message := command_calls
		private_message_button.pressed.emit()
		(commands._form_fields["message"] as LineEdit).text = "ID route"
		commands._submit_form()
		_check(command_calls == before_private_message + 1, "private ship-owner message did not emit a command")
		_check(commands._form_action.get("fixed", {}) == {"to_id": 17}, "private ship-owner message did not preserve the authoritative owner ID")
	_check(attack_button != null and not attack_button.disabled, "confirmed selected-ship action was incorrectly disabled")
	_check(tow_button != null and not tow_button.disabled, "implemented tow action was not available for a ship with a confirmed ID")
	view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": 18, "ship_id": 75},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": {"id": 18, "adjacent_sector_ids": [], "ports": [], "planets": [], "ships": [{"ship_id": 75, "name": "My Active Ship"}]},
	})
	for child in view.context_action_list.get_children():
		child.free()
	view.context_action_buttons.clear()
	view._add_object_actions("ship", {"data": {"ship_id": 75}})
	_check(_context_button(view, "TOW THIS SHIP") == null, "active ship was exposed as a tow target")
	_check(_context_button(view, "ACTIVE SHIP · no target actions") != null, "active ship target actions were not explained")
	for child in view.context_action_list.get_children():
		child.free()
	view.context_action_buttons.clear()
	view._add_object_actions("ship", {"data": {"name": "Unidentified vessel"}})
	_check(_context_button(view, "TOW THIS SHIP") == null, "ship without an ID exposed a tow action")
	var missing_ship_id := _context_button(view, "SHIP ACTIONS · ID UNAVAILABLE")
	_check(missing_ship_id != null and missing_ship_id.disabled, "missing ship ID did not explain why target actions are unavailable")
	for child in view.context_action_list.get_children():
		child.free()
	view.context_action_buttons.clear()
	view._add_object_actions("ship", {"data": {"ship_id": true, "name": "Boolean-ID vessel"}})
	var boolean_ship_id := _context_button(view, "SHIP ACTIONS · ID UNAVAILABLE")
	_check(boolean_ship_id != null and boolean_ship_id.disabled, "boolean ship metadata was coerced into target actions")
	_check(_context_button(view, "INSPECT SHIP DETAILS · ID UNAVAILABLE") != null, "boolean ship metadata exposed inspection")
	for child in view.context_action_list.get_children():
		child.free()
	view.context_action_buttons.clear()
	view._add_object_actions("ship", {"data": {"ship_id": 74.0, "name": "Fractional-ID vessel"}})
	var fractional_ship_id := _context_button(view, "SHIP ACTIONS · ID UNAVAILABLE")
	_check(fractional_ship_id != null and fractional_ship_id.disabled, "fractional ship metadata was coerced into target actions")
	_check(_context_button(view, "INSPECT SHIP DETAILS · ID UNAVAILABLE") != null, "fractional ship metadata exposed inspection")
	view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": 18, "ship_id": 74},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"ship": {"cargo": [{"commodity": "ORE", "quantity": 4}], "holds": 20},
		"sector": {"id": 18, "ports": [], "planets": [], "ships": [{"ship_id": 74.0, "name": "Fractional metadata"}]}
	})
	view.select_object_key("ship:74")
	_check(not view.selection_detail.text.contains("Holds"), "fractional selected-ship ID was treated as the confirmed active ship")
	for child in view.context_action_list.get_children():
		child.free()
	view.context_action_buttons.clear()
	view._add_object_actions("planet", {"data": {"name": "Unidentified planet", "planet_id": 0}})
	var missing_planet_info := _context_button(view, "PLANET INFORMATION · ID UNAVAILABLE")
	var missing_planet_land := _context_button(view, "LAND ON PLANET · ID UNAVAILABLE")
	_check(missing_planet_info != null and missing_planet_info.disabled, "missing planet ID did not explain why information is unavailable")
	_check(missing_planet_land != null and missing_planet_land.disabled, "missing planet ID did not explain why landing is unavailable")
	var before_missing_planet_activation := command_calls
	view.sector_view.select_object("planet:unknown:unidentified_planet")
	view._on_art_object_activated("planet:unknown:unidentified_planet")
	_check(command_calls == before_missing_planet_activation, "artwork activation sent a landing request without an authoritative planet ID")
	_check(view.notification_label.text.contains("did not provide a planet ID"), "missing planet ID activation did not explain why landing is unavailable")
	for child in view.context_action_list.get_children():
		child.free()
	view.context_action_buttons.clear()
	view._add_object_actions("planet", {"data": {"planet_id": 77.5, "name": "Fractional metadata"}})
	var fractional_planet_land := _context_button(view, "LAND ON PLANET · ID UNAVAILABLE")
	_check(fractional_planet_land != null and fractional_planet_land.disabled, "fractional planet metadata was coerced into an actionable landing ID")
	var before_fractional_activation := command_calls
	view.sector_view.select_object("planet:unknown:fractional_metadata")
	view._on_art_object_activated("planet:unknown:fractional_metadata")
	_check(command_calls == before_fractional_activation, "fractional planet metadata sent a landing request")
	view.set_snapshot({
		"authenticated": true,
		"disconnected": true,
		"hud": {"sector_id": 17},
		"freshness": {"sector": "disconnected", "overall": "disconnected"},
		"sector": {"id": 17, "adjacent_sector_ids": [18], "ports": [], "planets": [], "ships": []},
	})
	view.request_warp_now(18)
	_check(activations == [18], "stale/disconnected state allowed a warp")
	view.show_trade_routes({"routes": [{"port_a_id": 81, "port_b_id": 92, "sector_a_id": 18, "sector_b_id": 24, "hops_between": 3, "hops_from_player": 1, "is_two_way": true}]})
	await process_frame
	_check(view._trade_route_rows.get_child_count() == 1, "legacy route response did not render a recommendation")
	if view._trade_route_rows.get_child_count() == 1:
		var route_line: HBoxContainer = view._trade_route_rows.get_child(0).get_child(0).get_child(0)
		var route_detail: Label = route_line.get_child(0)
		var route_plot: Button = route_line.get_child(1)
		_check(route_detail.text.contains("Port 81") and route_detail.text.contains("trade in both directions"), "legacy route response lost its port or direction details")
		_check(route_detail.text.contains("Price estimate unavailable") and route_plot.text == "PLOT TO 18", "legacy route response did not explain missing estimates or offer a usable plot target")
	view.show_restored_autopilot_route([18, 21, 24], 24, "stopped", "manual")
	_check(view._route_panel.visible and view._route_label.text.contains("SAVED ROUTE · STOPPED") and view._route_label.text.contains("18 → 21 → 24"), "reconnected persisted autopilot route was not presented with its authoritative state")
	_check(command_calls == 0, "restoring a saved route moved the ship unexpectedly")
	_check(view._route_panel.get_child_count() == 1, "route status panel still contains redundant execution buttons")
	view.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot gameplay view tests: 22 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)

func _context_button(view, label: String) -> Button:
	for child in view.context_action_list.get_children():
		if child is Button and (child as Button).text == label:
			return child
	return null
