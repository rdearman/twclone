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
	menu._snapshot = {"authenticated": true, "disconnected": false, "hud": {"sector_id": 17}}
	menu._active_category = "COMPUTER"
	menu._render_actions()
	var status_button := _action_button(menu, "Ship towing status")
	_check(status_button != null, "towing status was not reachable from the computer menu")
	if status_button != null:
		status_button.pressed.emit()
	_check(captured.size() == 1 and captured[0]["command"] == "ship.tow.status" and not captured[0]["mutating"], "towing status did not use the documented read command")
	var release_button := _action_button(menu, "Release current tow beam")
	_check(release_button != null, "tow release was not reachable from the computer menu")
	if release_button != null:
		release_button.pressed.emit()
		menu._form_dialog.confirmed.emit()
	_check(captured.size() == 2 and captured[1]["command"] == "ship.tow" and captured[1]["data"].is_empty() and captured[1]["mutating"], "tow release did not send the documented empty disengage request")
	menu._active_category = "NAVIGATION"
	menu._render_actions()
	var transwarp_button := _action_button(menu, "Transwarp to sector")
	_check(transwarp_button != null, "transwarp was not reachable in navigation")
	if transwarp_button != null:
		transwarp_button.pressed.emit()
		menu._form_fields["to_sector_id"].text = "77"
		menu._submit_form()
	_check(captured.size() == 3 and captured[2]["command"] == "move.transwarp" and captured[2]["data"] == {"to_sector_id": 77}, "transwarp did not use the documented destination payload")
	menu.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot ship/navigation tests: 7 checks passed")
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
