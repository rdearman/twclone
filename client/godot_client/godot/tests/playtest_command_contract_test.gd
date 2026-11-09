extends SceneTree

const CommandMenu = preload("res://CommandMenu.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var menu := CommandMenu.new()
	root.add_child(menu)
	await process_frame
	var captured: Array = []
	menu.command_requested.connect(func(command: String, data: Dictionary, _label: String, _mutating: bool) -> void:
		captured.append({"command": command, "data": data})
	)
	menu._snapshot = {"authenticated": true, "disconnected": false, "hud": {"sector_id": 17}, "freshness": {"sector": "fresh"}}

	_check(menu._form_scroll != null and menu._form_content.get_parent() == menu._form_scroll, "command forms are not bounded by a scroll container")
	var tow := _command("Release current tow beam")
	_check(tow.get("command") == "ship.tow" and tow.get("fields", []).is_empty(), "tow release has unrelated cargo fields")
	menu._choose_action(tow)
	_check(not menu._form_dialog.dialog_text.to_lower().contains("cargo"), "tow release reused a cargo-destruction warning")

	var jettison := _command("Jettison cargo")
	menu._choose_action(jettison)
	_check(menu._form_fields.has("commodity") and menu._form_fields.has("quantity"), "jettison lost its commodity/quantity fields")
	_check(menu._form_content.get_child_count() > 0 and (menu._form_content.get_child(0) as Label).text.to_lower().contains("permanently destroys"), "jettison warning is missing")

	var hardware := _command("Buy ship hardware")
	_check(hardware.get("fields", [])[0][0] == "code", "hardware purchase does not use the server's code field")
	menu.show_hardware_catalogue({"location_type": "STARDOCK", "items": [{"code": "TRANSWARP", "name": "Transwarp drive", "price": 500, "max_purchase": 1, "ship_has_capacity": true}]})
	await process_frame
	_check(menu._shipyard_rows.get_child_count() == 1 and not (menu._shipyard_rows.get_child(0) as Button).disabled, "Godot did not render an eligible server-listed hardware option")
	(menu._shipyard_rows.get_child(0) as Button).pressed.emit()
	(menu._form_fields["quantity"] as LineEdit).text = "1"
	menu._submit_form()
	_check(captured.size() == 1 and captured[0]["command"] == "hardware.buy" and captured[0]["data"].get("code") == "TRANSWARP", "Godot hardware selection did not submit the authoritative code")
	var sweep := _command("Sweep mines")
	_check(sweep.get("command") == "combat.sweep_mines" and sweep.get("context_sector", false), "Godot mine sweep does not use the shared target-sector contract")
	var beacon := _command("Deploy marker beacon")
	var genesis := _command("Create planet with Genesis Torpedo")
	_check(bool(beacon.get("requires_fresh_sector", false)) and bool(genesis.get("requires_fresh_sector", false)), "sector-sensitive operations were not gated on fresh authoritative state")
	menu._active_category = "COMPUTER"
	menu._render_actions()
	await process_frame
	for row in menu._action_list.get_children():
		if row is HBoxContainer:
			failures.append("normal command list still renders a multi-column action row")
		if row is Button:
			_check(not ((row as Button).text in ["★", "☆", "KEY", "…"]), "normal command list still renders a favourite/shortcut control")

	menu.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot playtest command contract tests: PASS")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _command(label: String) -> Dictionary:
	for category in CommandMenu.COMMANDS.values():
		for action in category:
			if str(action.get("label", "")) == label:
				return action
	return {}

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
