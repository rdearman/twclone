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
	menu._snapshot = {"authenticated": true, "disconnected": false}
	var action := {"label": "Transfer owned ship by roster ID", "command": "ship.transfer",
		"fields": [["ship_id", "integer", "Ship ID", ""], ["target_player_id", "integer", "Recipient", ""]],
		"mutating": true}

	menu.set_owned_ship_roster({"ships": [{"ship_id": 88, "active": false}]})
	menu._choose_action(action)
	(menu._form_fields["ship_id"] as LineEdit).text = "88"
	(menu._form_fields["target_player_id"] as LineEdit).text = "42"
	menu._submit_form()
	_check(captured.size() == 1, "roster-listed ship transfer did not emit")
	if captured.size() == 1:
		_check(captured[0]["data"] == {"ship_id": 88, "target_player_id": 42}, "transfer did not use the server-listed ship ID")

	menu.set_owned_ship_roster({})
	menu._choose_action(action)
	(menu._form_fields["ship_id"] as LineEdit).text = "88"
	(menu._form_fields["target_player_id"] as LineEdit).text = "42"
	menu._submit_form()
	_check(captured.size() == 1, "transfer emitted without a confirmed owned-ship roster")
	_check(menu._result_text.text.contains("refresh Owned ship roster"), "missing roster gate was not explained")

	menu.set_owned_ship_roster({"ships": [{"ship_id": 88, "active": "false"}]})
	menu._choose_action(action)
	(menu._form_fields["ship_id"] as LineEdit).text = "88"
	(menu._form_fields["target_player_id"] as LineEdit).text = "42"
	menu._submit_form()
	_check(captured.size() == 1, "transfer emitted from a roster with an untyped active flag")
	_check(menu._result_text.text.contains("refresh Owned ship roster"), "untyped active flag was not rejected")

	menu.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 ship transfer roster gate tests: 4 checks passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
