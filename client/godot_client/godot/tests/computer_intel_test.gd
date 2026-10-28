extends SceneTree

const CommandMenu = preload("res://CommandMenu.gd")

var failures: Array[String] = []
var captured: Array[Dictionary] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var menu = CommandMenu.new()
	root.add_child(menu)
	await process_frame
	menu.command_requested.connect(func(command: String, data: Dictionary, label: String, mutating: bool) -> void:
		captured.append({"command": command, "data": data, "label": label, "mutating": mutating})
	)
	menu._snapshot = {"authenticated": true, "disconnected": false, "hud": {"sector_id": 22}}
	menu._active_category = "COMPUTER"
	menu._render_actions()
	for entry in [
		{"label": "Current sector information", "command": "sector.info"},
		{"label": "Detailed current-sector scan", "command": "sector.scan"},
	]:
		var button := _action_button(menu, entry["label"])
		_check(button != null, "%s was not exposed in the computer" % entry["label"])
		if button != null:
			button.pressed.emit()
		var latest: Dictionary = captured.back() if not captured.is_empty() else {}
		_check(latest.get("command") == entry["command"] and latest.get("data", {}).is_empty() and not latest.get("mutating", true), "%s did not use a read-only current-state request" % entry["label"])
	menu._active_category = "OPERATIONS"
	menu._render_actions()
	await process_frame
	var status_button := _action_button(menu, "Combat status")
	_check(status_button != null, "combat status was not exposed in operations")
	if status_button != null:
		status_button.pressed.emit()
	var latest_status: Dictionary = captured.back() if not captured.is_empty() else {}
	_check(latest_status.get("command") == "combat.status" and latest_status.get("data", {}).is_empty() and not latest_status.get("mutating", true), "combat status did not use the read-only server command")
	menu._active_category = "COMPUTER"
	menu._render_actions()
	await process_frame
	_check(_action_button(menu, "Search known space") == null, "unfiltered global search was exposed despite the fog-of-war boundary")
	menu._snapshot["hud"]["ship_id"] = 10
	menu._snapshot["freshness"] = {"sector": "fresh"}
	menu._selection = {"kind": "ship", "name": "Target", "data": {"ship_id": 20}}
	menu._active_category = "SHIP"
	menu._render_actions()
	await process_frame
	var attack := _action_button(menu, "Attack this ship")
	_check(attack != null and not attack.disabled, "attack action was unavailable outside protected FedSpace")
	menu._snapshot["hud"]["sector_id"] = 10
	menu._render_actions()
	await process_frame
	attack = _action_button(menu, "Attack this ship")
	_check(attack != null and attack.disabled and attack.tooltip_text.contains("FedSpace"), "attack was not blocked in protected FedSpace")
	_check(captured.size() == 3, "inspection, scan or combat status unexpectedly sent an extra command")
	menu.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot computer/intel tests: 9 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _action_button(menu: CommandMenu, label_text: String) -> Button:
	for row in menu._action_list.get_children():
		for child in row.get_children():
			if child is Button and (child as Button).text == label_text:
				return child
	return null

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
