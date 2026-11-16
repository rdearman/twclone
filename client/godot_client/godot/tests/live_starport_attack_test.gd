extends SceneTree

const MainScene = preload("res://Main.tscn")

var app
var failures: Array[String] = []
var checks := 0
var target_port_id := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if OS.get_environment("TWCLONE_GODOT_PORT_ATTACK_LIVE") != "1":
		print("SKIP: set TWCLONE_GODOT_PORT_ATTACK_LIVE=1 for disposable StarPort attack acceptance")
		quit(0)
		return
	target_port_id = int(OS.get_environment("TWCLONE_GODOT_TARGET_PORT"))
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
		var state: Dictionary = app.client_state.snapshot()
		var freshness: Dictionary = state.get("freshness", {})
		return app.auth_session.is_authenticated() and freshness.get("sector") == "fresh" \
			and freshness.get("ship") == "fresh",
		240)
	_check(authenticated, "ordinary player did not authenticate and refresh sector/ship through Godot UI")
	if not authenticated or target_port_id <= 0:
		_finish()
		return
	var snapshot: Dictionary = app.client_state.snapshot()
	var ports: Array = snapshot.get("sector", {}).get("ports", [])
	var target_present := false
	for row in ports:
		if row is Dictionary and int(row.get("port_id", row.get("id", 0))) == target_port_id:
			target_present = true
	_check(target_present, "target StarPort was not in the authoritative current-sector response")
	if not target_present:
		_finish()
		return
	app.gameplay_view.select_object_key("port:%d" % target_port_id)
	await process_frame
	var attack_button: Button
	for button in app.gameplay_view.context_action_buttons:
		if button.text == "ATTACK THIS STARPORT":
			attack_button = button
			break
	_check(attack_button != null and not attack_button.disabled,
		"confirmed current-sector StarPort did not expose an enabled attack UI action")
	if attack_button == null or attack_button.disabled:
		_finish()
		return
	attack_button.pressed.emit()
	await process_frame
	_check(app.gameplay_view.command_menu._form_dialog.visible,
		"StarPort attack did not require explicit user confirmation")
	if app.gameplay_view.command_menu._form_dialog.visible:
		app.gameplay_view.command_menu._form_dialog.get_ok_button().pressed.emit()
	var settled := await _wait_until(func() -> bool:
		return app._pending_game_commands.is_empty() and not app._command_refresh_pending \
			and not app.gameplay_view.command_menu._busy,
		300)
	_check(settled, "StarPort attack command or authoritative refresh did not settle")
	var after: Dictionary = app.client_state.snapshot()
	var remains := false
	for row in after.get("sector", {}).get("ports", []):
		if row is Dictionary and int(row.get("port_id", row.get("id", 0))) == target_port_id:
			remains = true
	_check(not remains, "destroyed StarPort remained in the fresh sector view")
	_check(app._pending_port_attack.is_empty(), "server reply did not clear the completed idempotent StarPort request")
	if failures.is_empty():
		print("LIVE_PORT_ATTACK_PASS client=Godot player_id=%s port_id=%d" % [str(app.auth_session.player_id), target_port_id])
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
	print("Godot StarPort attack UI checks: %d passed" % checks)
	quit(0)
