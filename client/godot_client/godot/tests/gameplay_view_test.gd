extends SceneTree

const GameplayView = preload("res://GameplayView.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var view = GameplayView.new()
	root.add_child(view)
	await process_frame
	var commands = view.command_menu
	_check(commands is PanelContainer, "general commands are not a docked panel")
	_check(commands.get_parent() == view.gameplay_body and view.gameplay_body.get_child(0) == commands, "command panel is not persistently placed on the left")
	_check(is_equal_approx(view.toast_panel.anchor_left, 0.5) and is_equal_approx(view.toast_panel.anchor_right, 0.5), "notification ribbon is not centered at the bottom")
	view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": 17},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": {"id": 17, "adjacent_sector_ids": [18], "ports": [], "planets": [], "ships": []},
	})
	view.show_command_result("Sector density scan", {"sectors": [21, 21.0, 213, 100.0, 808, 0, 1793, null]})
	_check(not view.command_menu._result_dialog.visible, "read-only result opened a modal dialog")
	_check(view.information_text.text.contains("21: 21") and view.information_text.text.contains("1793: unavailable"), "density result was not rendered in the Information panel")
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
	_check(not view.command_menu._result_dialog.visible and view.information_text.text.contains("21: 21"), "port selection covered or replaced the Information panel")
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
		"sector": {"id": 18, "adjacent_sector_ids": [17], "ports": [], "planets": [], "ships": [{"ship_id": 74, "name": "Long Meridian"}]},
	})
	_check(not view.port_workflow.visible and not commands._tavern_access, "authoritative movement did not close the old port/Tavern context")
	view.select_object_key("ship:74")
	var inspect_button := _context_button(view, "INSPECT SHIP DETAILS · unavailable")
	var private_message_button := _context_button(view, "PRIVATE MESSAGE OWNER · unavailable")
	var attack_button := _context_button(view, "ATTACK THIS SHIP")
	_check(inspect_button != null and inspect_button.disabled and inspect_button.tooltip_text.contains("ignores it"), "unsupported ship inspection was not visibly disabled with its reason")
	_check(private_message_button != null and private_message_button.disabled, "unsupported ship messaging was not visibly disabled")
	_check(attack_button != null and not attack_button.disabled, "confirmed selected-ship action was incorrectly disabled")
	view.set_snapshot({
		"authenticated": true,
		"disconnected": true,
		"hud": {"sector_id": 17},
		"freshness": {"sector": "disconnected", "overall": "disconnected"},
		"sector": {"id": 17, "adjacent_sector_ids": [18], "ports": [], "planets": [], "ships": []},
	})
	view.request_warp_now(18)
	_check(activations == [18], "stale/disconnected state allowed a warp")
	view.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot gameplay view tests: 13 passed")
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
