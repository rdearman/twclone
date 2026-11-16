extends SceneTree

const MainScene = preload("res://Main.tscn")

var app
var failures: Array[String] = []
var checks := 0
var planet_id := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if OS.get_environment("TWCLONE_GODOT_PLANET_LIVE") != "1":
		print("SKIP: set TWCLONE_GODOT_PLANET_LIVE=1 for disposable persisted-planet acceptance")
		quit(0)
		return
	planet_id = int(OS.get_environment("TWCLONE_GODOT_PLANET_ID"))
	app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	var login = app.get_node("LoginView")
	login.host_input.text = "127.0.0.1"
	login.port_input.text = OS.get_environment("TWCLONE_GODOT_PORT")
	login.username_input.text = OS.get_environment("TWCLONE_GODOT_USERNAME")
	login.password_input.text = OS.get_environment("TWCLONE_GODOT_PASSWORD")
	login.connect_button.pressed.emit()
	var restored := await _wait_until(func() -> bool:
		return app.auth_session.is_authenticated() \
			and app._landed_planet_id == planet_id \
			and app.gameplay_view.planet_workflow.visible \
			and app.gameplay_view.planet_workflow._surface_identity_confirmed,
			300)
	_check(restored, "fresh Godot login did not reconcile the persisted landed planet and expose its confirmed surface")
	if not restored:
		print("LIVE_PLANET snapshot=%s landed=%d ready=%s visible=%s" % [str(app.client_state.snapshot()), app._landed_planet_id, str(app._landing_surface_ready), str(app.gameplay_view.planet_workflow.visible)])
		_finish()
		return
	var workflow = app.gameplay_view.planet_workflow
	_check(workflow._labor_save != null and not workflow._labor_save.disabled,
		"confirmed planet surface did not enable the workforce action")
	workflow._labor_ore.value = 101
	workflow._labor_organics.value = 100
	workflow._labor_equipment.value = 100
	workflow._labor_weapons.value = 0
	var refresh_before: int = int(app.client_state.snapshot().get("refresh_generation", 0))
	workflow._labor_save.pressed.emit()
	var allocation_settled := await _wait_until(func() -> bool:
		return app._pending_game_commands.is_empty() and not app._command_refresh_pending \
			and int(app.client_state.snapshot().get("refresh_generation", 0)) > refresh_before \
			and int(workflow._labor_ore.value) == 101,
			300)
	_check(allocation_settled, "Godot planet workforce UI action did not settle with the server-confirmed assignment")
	_check(app.client_state.snapshot().get("ship", {}).get("onplanet", null) == 1,
		"ship status no longer confirms the landed state after workforce assignment")
	var launch_button: Button
	for node in workflow.get_children():
		launch_button = _find_button(node, "LAUNCH TO SECTOR")
		if launch_button != null:
			break
	_check(launch_button != null and not launch_button.disabled, "planet surface did not expose an enabled launch action")
	if launch_button != null and not launch_button.disabled:
		launch_button.pressed.emit()
		var launched := await _wait_until(func() -> bool:
			return app._landed_planet_id == 0 and not workflow.visible \
				and int(app.client_state.snapshot().get("ship", {}).get("onplanet", -1)) == 0,
			300)
		_check(launched, "Godot launch action did not close the surface after authoritative departure refresh")
	if failures.is_empty():
		print("LIVE_PLANET_PASS client=Godot planet_id=%d player_id=%s" % [planet_id, str(app.auth_session.player_id)])
	_finish()

func _find_button(node: Node, label: String) -> Button:
	if node is Button and node.text == label:
		return node
	for child in node.get_children():
		var found := _find_button(child, label)
		if found != null:
			return found
	return null

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
	print("Godot persisted planet UI checks: %d passed" % checks)
	quit(0)
