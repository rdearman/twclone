extends SceneTree

const CommandMenu = preload("res://CommandMenu.gd")

var failures: Array[String] = []
var checks := 0
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
	menu._active_category = "SETTINGS & NOTES"
	menu._render_actions()

	var petty_cash := _action_button(menu, "Use petty cash for trades by default")
	_check(petty_cash != null and not petty_cash.disabled, "petty-cash trade preference was not available in normal settings")
	if petty_cash != null:
		petty_cash.pressed.emit()
		menu._submit_form()
	_check(captured.size() == 1, "petty-cash preference did not emit exactly one request")
	if captured.size() == 1:
		_check(captured[0]["command"] == "player.set_trade_account_preference", "petty-cash preference used the wrong command")
		_check(captured[0]["data"] == {"preference": 0}, "petty-cash preference did not use the authoritative schema payload")
		_check(captured[0]["mutating"], "petty-cash preference was not marked as mutating")

	var bank := _action_button(menu, "Use bank for trades by default")
	_check(bank != null and not bank.disabled, "bank trade preference was not available in normal settings")
	if bank != null:
		bank.pressed.emit()
		menu._submit_form()
	_check(captured.size() == 2, "bank preference did not emit exactly one request")
	if captured.size() == 2:
		_check(captured[1]["command"] == "player.set_trade_account_preference", "bank preference used the wrong command")
		_check(captured[1]["data"] == {"preference": 1}, "bank preference did not use the authoritative schema payload")
		_check(captured[1]["mutating"], "bank preference was not marked as mutating")

	menu.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot trade preference tests: %d checks passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

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

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
