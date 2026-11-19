extends SceneTree

const MainScene = preload("res://Main.tscn")

var app
var failures: Array[String] = []
var checks := 0
var target_planet_id := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if OS.get_environment("TWCLONE_GODOT_CAPTURE_LIVE") != "1":
		print("SKIP: set TWCLONE_GODOT_CAPTURE_LIVE=1 for disposable planet-capture acceptance")
		quit(0)
		return
	target_planet_id = int(OS.get_environment("TWCLONE_GODOT_CAPTURE_PLANET_ID"))
	app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	var login = app.get_node("LoginView")
	login.host_input.text = "127.0.0.1"
	login.port_input.text = OS.get_environment("TWCLONE_GODOT_PORT")
	login.username_input.text = OS.get_environment("TWCLONE_GODOT_USERNAME")
	login.password_input.text = OS.get_environment("TWCLONE_GODOT_PASSWORD")
	login.connect_button.pressed.emit()
	var ready := await _wait_until(func() -> bool:
		var snapshot: Dictionary = app.client_state.snapshot()
		var freshness: Dictionary = snapshot.get("freshness", {})
		return app.auth_session.is_authenticated() and freshness.get("sector") == "fresh" \
			and freshness.get("ship") == "fresh" and app._pending_game_commands.is_empty() \
			and not app._command_refresh_pending,
		300)
	_check(ready, "ordinary attacker did not authenticate and receive fresh authoritative ship/sector state")
	if not ready or target_planet_id <= 0:
		_finish()
		return
	var snapshot: Dictionary = app.client_state.snapshot()
	var planet_present := false
	for row in snapshot.get("sector", {}).get("planets", []):
		if row is Dictionary and int(row.get("planet_id", row.get("id", 0))) == target_planet_id:
			planet_present = true
	_check(planet_present, "target planet was absent from the current-sector response")
	if not planet_present:
		_finish()
		return
	var menu = app.gameplay_view.command_menu
	var refresh_before := int(snapshot.get("refresh_generation", 0))
	app.gameplay_view.select_object_key("planet:%d" % target_planet_id)
	await process_frame
	menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
	await process_frame
	var attack_button: Button
	for button in menu._action_list.get_children():
		if button is Button and button.text == "Attack this planet":
			attack_button = button
			break
	_check(attack_button != null and not attack_button.disabled,
		"selected current-sector planet did not expose an enabled Attack this planet command-menu action")
	if attack_button == null or attack_button.disabled:
		_finish()
		return
	attack_button.pressed.emit()
	var form_ready := await _wait_until(func() -> bool: return menu._inline_form_panel.visible, 60)
	_check(form_ready, "planet attack did not display its explicit population-choice confirmation form")
	if not form_ready:
		_finish()
		return
	var choice: Variant = menu._form_fields.get("enslave_colonists", null)
	_check(choice is CheckBox, "planet attack form did not expose the approved enslavement choice")
	if choice is CheckBox:
		(choice as CheckBox).button_pressed = true
	menu._inline_submit.pressed.emit()
	var settled := await _wait_until(func() -> bool:
		return app._pending_game_commands.is_empty() and not app._command_refresh_pending \
			and not menu._busy and int(app.client_state.snapshot().get("refresh_generation", 0)) > refresh_before,
		360)
	_check(settled, "Godot planet-attack request and authoritative post-combat refresh did not settle")
	var result_text := str(app.gameplay_view.notification_label.text)
	_check(result_text.contains("Planet %d · captured: true · colonists enslaved: true" % target_planet_id),
		"client did not present the server-confirmed capture and selected population outcome: %s" % result_text)
	var fighter_receipt_text := result_text.get_slice("your fighters remaining: ", 1).get_slice(" ", 0)
	var refreshed_ship: Dictionary = app.client_state.snapshot().get("ship", {})
	_check(fighter_receipt_text.is_valid_int(), "capture result did not include a server-reported remaining fighter count")
	_check(typeof(refreshed_ship.get("fighters", null)) == TYPE_INT and fighter_receipt_text.is_valid_int()
		and int(refreshed_ship.get("fighters", -1)) == int(fighter_receipt_text),
		"post-combat ship refresh disagreed with the fighter count in the authoritative attack response")
	_check(app.auth_session.is_authenticated(), "player session was lost during the capture workflow")
	if failures.is_empty():
		print("LIVE_PLANET_CAPTURE_PASS client=Godot player_id=%s planet_id=%d" % [str(app.auth_session.player_id), target_planet_id])
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
	print("Godot planet-capture UI checks: %d passed" % checks)
	quit(0)
