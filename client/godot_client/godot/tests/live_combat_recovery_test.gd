extends SceneTree

const MainScene = preload("res://Main.tscn")

var app
var failures: Array[String] = []
var checks := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if OS.get_environment("TWCLONE_GODOT_RECOVERY_LIVE") != "1":
		print("SKIP: set TWCLONE_GODOT_RECOVERY_LIVE=1 for disposable escape-pod recovery acceptance")
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
	var recovered := await _wait_until(func() -> bool:
		var state: Dictionary = app.client_state.snapshot()
		var freshness: Dictionary = state.get("freshness", {})
		return app.auth_session.is_authenticated() and freshness.get("player") == "fresh" \
			and freshness.get("ship") == "fresh" and freshness.get("sector") == "fresh",
			240)
	_check(recovered, "ordinary player's post-restart recovery login did not receive authoritative player/ship/sector state")
	if recovered:
		var state: Dictionary = app.client_state.snapshot()
		var player: Dictionary = state.get("player", {})
		var ship: Dictionary = state.get("ship", {})
		var expected_id := int(OS.get_environment("TWCLONE_GODOT_EXPECTED_PLAYER_ID"))
		if expected_id > 0:
			_check(int(player.get("id", 0)) == expected_id, "recovery login restored the wrong player identity")
		var active_ship_id := int(player.get("ship_id", 0))
		_check(active_ship_id > 0 and int(ship.get("id", 0)) == active_ship_id,
			"recovery login did not restore the player's authoritative active ship")
		_check(str(ship.get("name", "")).to_lower().contains("escape pod"),
			"destroyed player did not recover into the server-assigned Escape Pod")
		_check(int(ship.get("hull", 0)) == 100 and int(ship.get("onplanet", -1)) == 0,
			"recovery ship status is not a usable pod in space")
		_check(int(player.get("sector_id", 0)) == 1 and int(state.get("hud", {}).get("ship_id", 0)) == active_ship_id,
			"recovery location or player-visible active-ship HUD did not match the authoritative pod")
	if failures.is_empty():
		print("LIVE_RECOVERY_PASS client=Godot player_id=%s ship_id=%s" % [str(app.auth_session.player_id), str(app.client_state.snapshot().get("ship", {}).get("id", 0))])
		print("Godot escape-pod recovery checks: %d passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

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
