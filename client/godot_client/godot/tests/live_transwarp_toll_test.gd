extends SceneTree

const MainScene = preload("res://Main.tscn")
const PASSWORD := "Temporary-Toll-QA-Password-42"

var failures: Array[String] = []
var apps: Array = []
var passer
var passer_username := ""

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if OS.get_environment("TWCLONE_GODOT_TOLL_LIVE") != "1":
		print("SKIP: set TWCLONE_GODOT_TOLL_LIVE=1 for disposable transwarp toll acceptance")
		quit(0)
		return
	root.size = Vector2i(1280, 720)
	randomize()
	var suffix := str(randi_range(100000, 999999))
	var owner_a = await _register_player("gdtoll_a_" + suffix)
	var owner_b = await _register_player("gdtoll_b_" + suffix)
	passer_username = "gdtoll_p_" + suffix
	passer = await _register_player(passer_username)
	if owner_a == null or owner_b == null or passer == null:
		_finish()
		return
	var identities := {
		"owner_a": _identity(owner_a),
		"owner_b": _identity(owner_b),
		"passer": _identity(passer),
	}
	var ready_path := OS.get_environment("TWCLONE_GODOT_TOLL_READY_FILE")
	var fixture_path := OS.get_environment("TWCLONE_GODOT_TOLL_FIXTURE_FILE")
	if ready_path.is_empty() or fixture_path.is_empty():
		_check(false, "acceptance harness needs ready and fixture marker paths")
		_finish()
		return
	var ready := FileAccess.open(ready_path, FileAccess.WRITE)
	if ready == null:
		_check(false, "could not publish disposable player IDs for fixture setup")
		_finish()
		return
	ready.store_string(JSON.stringify(identities))
	ready.close()
	_check(await _wait_for_file(fixture_path, 2400), "timed out waiting for isolated database toll fixtures")
	if failures.size() > 0:
		_finish()
		return
	# Refresh through the normal client transport after deterministic SQL fixture
	# setup; all gameplay actions below are real menu handlers.
	passer._refresh_after_command(["player", "ship", "sector"])
	var refreshed := await _wait_until(func() -> bool:
		return passer.client_state.snapshot().get("freshness", {}).get("ship") == "fresh" and not passer._command_refresh_pending,
		180)
	_check(refreshed, "authoritative post-fixture ship refresh did not settle")
	var before: Dictionary = passer.client_state.snapshot()
	var start_credits := _credits(before)
	var start_shields := _shields(before)
	_check(start_credits >= 6 and start_shields >= 20, "fixture player lacked toll funds or shields")
	_check(await _transwarp(passer, 20, true, 4), "Godot UI payment and passage failed")
	var paid: Dictionary = passer.client_state.snapshot()
	_check(_credits(paid) == start_credits - 4, "accepted toll did not debit exactly four credits in the client state")
	_check(_shields(paid) == start_shields, "paid passage incorrectly engaged toll fighters")
	_check(await _reconnect_passer(20), "server-restart reconnect did not restore the paid destination")
	var before_refusal: Dictionary = passer.client_state.snapshot()
	var refusal_credits := _credits(before_refusal)
	var refusal_shields := _shields(before_refusal)
	_check(await _transwarp(passer, 21, false, 2), "Godot UI refusal and hostile encounter did not settle")
	var refused: Dictionary = passer.client_state.snapshot()
	_check(_credits(refused) == refusal_credits, "refusing the toll changed player credits")
	_check(_shields(refused) < refusal_shields, "refusal did not expose the client to toll-fighter damage")
	_check(await _reconnect_passer(21), "second reconnect did not restore refusal damage and destination")
	if failures.is_empty():
		print("LIVE_TRANSWARP_TOLL_PASS %s" % JSON.stringify({
			"passer_player_id": passer.auth_session.player_id,
			"accepted_sector": 20,
			"refused_sector": 21,
			"paid_amount": 4,
			"refusal_shield_loss": refusal_shields - _shields(passer.client_state.snapshot()),
			"ui_actions": 2,
			"reconnects": 2,
		}))
	_finish()

func _register_player(username: String):
	var app = MainScene.instantiate()
	apps.append(app)
	root.add_child(app)
	await process_frame
	var login = app.get_node("LoginView")
	login.host_input.text = "127.0.0.1"
	login.port_input.text = OS.get_environment("TWCLONE_GODOT_PORT")
	login.username_input.text = username
	login.password_input.text = PASSWORD
	login.mode_toggle_button.pressed.emit()
	login.confirm_password_input.text = PASSWORD
	login.ship_name_input.text = "Disposable toll QA"
	login.connect_button.pressed.emit()
	var authenticated := await _wait_until(func() -> bool:
		var freshness: Dictionary = app.client_state.snapshot().get("freshness", {})
		return app.auth_session.is_authenticated() and freshness.get("player") == "fresh" \
			and freshness.get("ship") == "fresh" and freshness.get("sector") == "fresh",
		240)
	_check(authenticated, "Godot registration/login failed for ordinary player %s: %s" % [username, str(login.status_label.text)])
	return app if authenticated else null

func _identity(app) -> Dictionary:
	var snapshot: Dictionary = app.client_state.snapshot()
	return {
		"player_id": int(app.auth_session.player_id),
		"ship_id": int(snapshot.get("hud", {}).get("ship_id", 0)),
		"sector_id": int(snapshot.get("hud", {}).get("sector_id", 0)),
	}

func _transwarp(app, destination: int, accept: bool, expected_amount: int) -> bool:
	var menu = app.gameplay_view.command_menu
	menu.select_category("NAVIGATION")
	await process_frame
	var button := _find_button(menu._action_list, "Transwarp to sector")
	if button == null or button.disabled:
		_check(false, "Navigation menu did not expose enabled transwarp")
		return false
	button.pressed.emit()
	if not menu._form_fields.has("to_sector_id"):
		_check(false, "Transwarp UI did not open its sector form")
		return false
	(menu._form_fields["to_sector_id"] as LineEdit).text = str(destination)
	menu._inline_submit.pressed.emit()
	if not await _wait_until(func() -> bool: return app._toll_offer_dialog.visible, 240):
		_check(false, "transwarp did not display the server toll offer")
		return false
	var offer_text: String = app._toll_offer_dialog.dialog_text
	_check(offer_text.contains("%d toll fighters" % expected_amount)
		and offer_text.contains("%d credits" % expected_amount), "Godot toll dialog did not display authoritative fighter/credit totals")
	if accept:
		app._toll_offer_dialog.get_ok_button().pressed.emit()
	else:
		app._toll_offer_dialog.get_cancel_button().pressed.emit()
	return await _wait_until(func() -> bool:
		return int(app.client_state.snapshot().get("hud", {}).get("sector_id", 0)) == destination \
			and app._pending_game_commands.is_empty() and not app._command_refresh_pending \
			and not menu._busy,
		300)

func _reconnect_passer(destination: int) -> bool:
	var menu = passer.gameplay_view.command_menu
	menu.select_category("COMPUTER")
	await process_frame
	var logout := _find_button(menu._action_list, "Log out to login screen")
	if logout == null:
		return false
	logout.pressed.emit()
	if not menu._logout_dialog.visible:
		return false
	menu._logout_dialog.get_ok_button().pressed.emit()
	if not await _wait_until(func() -> bool:
		return not passer.auth_session.is_authenticated() and passer.login_view.visible,
		120):
		return false
	if destination == 20:
		var restart_request := OS.get_environment("TWCLONE_GODOT_TOLL_RESTART_REQUEST")
		var restart_ready := OS.get_environment("TWCLONE_GODOT_TOLL_RESTART_READY")
		if restart_request.is_empty() or restart_ready.is_empty():
			return false
		var request_file := FileAccess.open(restart_request, FileAccess.WRITE)
		if request_file == null:
			return false
		request_file.store_string("restart-after-paid-transwarp\n")
		request_file.close()
		if not await _wait_for_file(restart_ready, 1200):
			return false
	if passer.login_view._registration_mode:
		passer.login_view.mode_toggle_button.pressed.emit()
	passer.login_view.host_input.text = "127.0.0.1"
	passer.login_view.port_input.text = OS.get_environment("TWCLONE_GODOT_PORT")
	passer.login_view.username_input.text = passer_username
	passer.login_view.password_input.text = PASSWORD
	passer.login_view.connect_button.pressed.emit()
	return await _wait_until(func() -> bool:
		var snapshot: Dictionary = passer.client_state.snapshot()
		var freshness: Dictionary = snapshot.get("freshness", {})
		return passer.auth_session.is_authenticated() \
			and freshness.get("player") == "fresh" \
			and freshness.get("ship") == "fresh" \
			and freshness.get("sector") == "fresh" \
			and int(snapshot.get("hud", {}).get("sector_id", 0)) == destination,
		240)

func _credits(snapshot: Dictionary) -> int:
	return int(snapshot.get("player", {}).get("credits", snapshot.get("hud", {}).get("credits", -1)))

func _shields(snapshot: Dictionary) -> int:
	return int(snapshot.get("ship", {}).get("shields", -1))

func _find_button(parent: Node, label: String) -> Button:
	for child in parent.get_children():
		if child is Button and (child as Button).text == label:
			return child
	return null

func _wait_until(predicate: Callable, frames: int) -> bool:
	for _frame in range(frames):
		if predicate.call():
			return true
		await process_frame
		await create_timer(0.02).timeout
	return predicate.call()

func _wait_for_file(path: String, frames: int) -> bool:
	return await _wait_until(func() -> bool: return FileAccess.file_exists(path), frames)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)

func _finish() -> void:
	if not failures.is_empty():
		for failure in failures:
			push_error(failure)
		quit(1)
	else:
		print("Godot live transwarp toll workflow passed")
		quit(0)
