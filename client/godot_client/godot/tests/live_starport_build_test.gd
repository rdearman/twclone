extends SceneTree

const MainScene = preload("res://Main.tscn")

var app
var failures: Array[String] = []
var checks := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if OS.get_environment("TWCLONE_GODOT_PORT_BUILD_LIVE") != "1":
		print("SKIP: set TWCLONE_GODOT_PORT_BUILD_LIVE=1 for disposable StarPort construction acceptance")
		quit(0)
		return
	app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	var login = app.get_node("LoginView")
	login.host_input.text = "127.0.0.1"
	login.port_input.text = OS.get_environment("TWCLONE_GODOT_PORT")
	login.username_input.text = OS.get_environment("TWCLONE_GODOT_USERNAME")
	login.password_input.text = OS.get_environment("TWCLONE_GODOT_PASSWORD")
	login.connect_button.pressed.emit()
	var refreshed := await _wait_until(func() -> bool:
		var state: Dictionary = app.client_state.snapshot()
		var freshness: Dictionary = state.get("freshness", {})
		return app.auth_session.is_authenticated() and freshness.get("sector") == "fresh" \
			and freshness.get("ship") == "fresh" and freshness.get("player") == "fresh",
		240)
	_check(refreshed, "ordinary player did not authenticate and refresh authoritative location and ship through the Godot UI")
	if not refreshed:
		await _finish()
		return
	var planet_visible := false
	var sector_snapshot: Dictionary = app.client_state.snapshot().get("sector", {})
	for planet in sector_snapshot.get("planets", []):
		if planet is Dictionary and int(planet.get("planet_id", planet.get("id", 0))) == 700:
			planet_visible = true
	_check(planet_visible, "the test planet was not present in the authoritative sector response")
	if not planet_visible:
		await _finish()
		return
	app.gameplay_view.select_object_key("planet:700")
	await process_frame
	var land_button: Button
	for button in app.gameplay_view.context_action_buttons:
		if button.text == "LAND ON PLANET":
			land_button = button
			break
	_check(land_button != null and not land_button.disabled, "the selected planet did not expose an enabled landing UI action")
	if land_button == null or land_button.disabled:
		await _finish()
		return
	land_button.pressed.emit()
	await process_frame
	var landed := await _wait_until(func() -> bool:
		return app._landed_planet_id == 700 and app._landing_surface_ready \
			and app._pending_game_commands.is_empty() and app._planet_pending_count == 0 \
			and not app._command_refresh_pending and not app.gameplay_view.command_menu._busy,
		320)
	_check(landed, "the server-confirmed landing did not open actionable planetary context")
	if not landed:
		await _finish()
		return
	var menu = app.gameplay_view.command_menu
	menu.open_for(app.client_state.snapshot(), {"kind": "planet", "name": "V3 construction test", "data": {"planet_id": 700}})
	await process_frame
	var construct_button: Button
	for child in menu._action_list.get_children():
		if child is Button and child.text.strip_edges().to_lower() == "construct starport":
			construct_button = child
			break
	_check(construct_button != null and not construct_button.disabled, "a confirmed landed owner could not access StarPort construction from the Planet menu")
	if construct_button == null or construct_button.disabled:
		await _finish()
		return
	construct_button.pressed.emit()
	await process_frame
	_check(menu._form_fields.has("port_class"), "StarPort class selection form did not open")
	if not menu._form_fields.has("port_class"):
		await _finish()
		return
	(menu._form_fields["port_class"] as LineEdit).text = "8"
	menu._submit_form()
	var started := await _wait_until(func() -> bool:
		return app._pending_game_commands.is_empty() and not app._command_refresh_pending \
			and not menu._busy,
		320)
	_check(started, "the actual StarPort construction request or authoritative refresh did not settle")
	_check(app._landed_planet_id == 700 and app._landing_surface_ready, "construction changed the client's confirmed landed-planet context")
	if failures.is_empty():
		print("LIVE_PORT_BUILD_PASS client=Godot player_id=%s planet_id=700 class=8" % str(app.auth_session.player_id))
	await _finish()

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
	if is_instance_valid(app):
		app.queue_free()
		await process_frame
	if not failures.is_empty():
		for failure in failures:
			push_error(failure)
		quit(1)
		return
	print("Godot StarPort construction UI checks: %d passed" % checks)
	quit(0)
