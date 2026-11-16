extends SceneTree

const MainScene = preload("res://Main.tscn")

var app
var failures: Array[String] = []
var checks := 0
var target_ship_id := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if OS.get_environment("TWCLONE_GODOT_COMBAT_LIVE") != "1":
		print("SKIP: set TWCLONE_GODOT_COMBAT_LIVE=1 for disposable two-player combat acceptance")
		quit(0)
		return
	target_ship_id = int(OS.get_environment("TWCLONE_GODOT_COMBAT_TARGET_SHIP"))
	app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	var login = app.get_node("LoginView")
	login.host_input.text = "127.0.0.1"
	login.port_input.text = OS.get_environment("TWCLONE_GODOT_PORT")
	login.username_input.text = OS.get_environment("TWCLONE_GODOT_USERNAME")
	login.password_input.text = OS.get_environment("TWCLONE_GODOT_PASSWORD")
	login.connect_button.pressed.emit()
	var authenticated := await _wait_until(func() -> bool:
		var snapshot: Dictionary = app.client_state.snapshot()
		var freshness: Dictionary = snapshot.get("freshness", {})
		return app.auth_session.is_authenticated() and freshness.get("sector") == "fresh" and freshness.get("ship") == "fresh",
		240)
	_check(authenticated, "attacker did not authenticate and refresh through the Godot login scene")
	if not authenticated or target_ship_id <= 0:
		_finish()
		return
	var snapshot: Dictionary = app.client_state.snapshot()
	var sector_ships: Array = snapshot.get("sector", {}).get("ships", [])
	var target_present := false
	for row in sector_ships:
		if row is Dictionary and int(row.get("ship_id", row.get("id", 0))) == target_ship_id:
			target_present = true
	_check(target_present, "target ship was not present in the authoritative current-sector snapshot")
	if not target_present:
		_finish()
		return
	var menu = app.gameplay_view.command_menu
	app.gameplay_view.select_object_key("ship:%d" % target_ship_id)
	await process_frame
	print("LIVE_COMBAT selected=%s" % str(app.gameplay_view.sector_view.selected_object()))
	var attack_button: Button
	for button in app.gameplay_view.context_action_buttons:
		if button.text == "ATTACK THIS SHIP":
			attack_button = button
			break
	_check(attack_button != null and not attack_button.disabled,
		"selected hostile ship did not expose an enabled Attack This Ship UI action")
	if attack_button == null or attack_button.disabled:
		_finish()
		return
	attack_button.pressed.emit()
	await process_frame
	_check(menu._form_dialog.visible, "ship attack did not present the client confirmation required for a mutating command")
	if menu._form_dialog.visible:
		menu._form_dialog.get_ok_button().pressed.emit()
	await process_frame
	print("LIVE_COMBAT after_click pending=%s refresh=%s notice=%s" % [
		str(app._pending_game_commands.keys()), str(app._command_refresh_pending),
		str(app.gameplay_view.notification_label.text),
	])
	var settled := await _wait_until(func() -> bool:
		return app._pending_game_commands.is_empty() and not app._command_refresh_pending and not menu._busy,
		300)
	_check(settled, "Godot ship attack or its authoritative refresh did not settle")
	var after: Dictionary = app.client_state.snapshot()
	var remains := false
	for row in after.get("sector", {}).get("ships", []):
		if row is Dictionary and int(row.get("ship_id", row.get("id", 0))) == target_ship_id:
			remains = true
	_check(not remains, "destroyed target remained visible after the server-confirmed attack and sector refresh")
	if failures.is_empty():
		print("LIVE_COMBAT_PASS client=Godot target_ship=%d attacker_player=%s" % [target_ship_id, str(app.auth_session.player_id)])
	_finish()

func _wait_until(predicate: Callable, max_frames: int) -> bool:
	for _frame in range(max_frames):
		await process_frame
		if predicate.call():
			return true
	return false

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)

func _finish() -> void:
	if not failures.is_empty():
		for failure in failures:
			push_error(failure)
		quit(1)
		return
	print("Godot disposable combat UI checks: %d passed" % checks)
	quit(0)
