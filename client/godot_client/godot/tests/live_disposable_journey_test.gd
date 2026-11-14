extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures: Array[String] = []
var app
var registered_username := ""
var test_password := "Temporary-QA-Password-42"

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if OS.get_environment("TWCLONE_GODOT_LIVE") != "1":
		print("SKIP: set TWCLONE_GODOT_LIVE=1 for a dedicated disposable server journey")
		quit(0)
		return
	app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	var login = app.get_node("LoginView")
	randomize()
	registered_username = "gd%d" % randi_range(10000000, 99999999)
	login.host_input.text = "127.0.0.1"
	login.port_input.text = "55499"
	login.username_input.text = registered_username
	login.password_input.text = test_password
	login.mode_toggle_button.pressed.emit()
	login.confirm_password_input.text = test_password
	login.ship_name_input.text = "Disposable QA"
	login.connect_button.pressed.emit()
	var authenticated := await _wait_until(func() -> bool:
		var snapshot: Dictionary = app.client_state.snapshot()
		var freshness: Dictionary = snapshot.get("freshness", {})
		return app.auth_session.is_authenticated() and freshness.get("player") == "fresh" \
			and freshness.get("ship") == "fresh" and freshness.get("sector") == "fresh",
		240
	)
	_check(authenticated, "ordinary player registration through the Godot LoginView did not authenticate")
	if not authenticated:
		_finish()
		return
	var initial: Dictionary = app.client_state.snapshot()
	var hud: Dictionary = initial.get("hud", {})
	var initial_sector: int = int(hud.get("sector_id", 0))
	var initial_turns: int = int(hud.get("turns_remaining", -1))
	var ship_id: int = int(hud.get("ship_id", 0))
	_check(initial_sector > 0 and initial_turns >= 0 and ship_id > 0, "starting sector, turns or active ship were absent after authoritative refresh")
	var adjacent: Array = initial.get("sector", {}).get("adjacent_sector_ids", [])
	_check(not adjacent.is_empty(), "confirmed sector did not expose connected navigation targets")
	if adjacent.is_empty():
		_finish()
		return
	var destination := int(adjacent[0])
	var menu = app.gameplay_view.command_menu
	menu.select_category("NAVIGATION")
	await process_frame
	var warp_button := _find_button(menu._action_list, "Warp to sector number")
	_check(warp_button != null and not warp_button.disabled, "navigation menu did not expose a usable direct warp action")
	if warp_button == null or warp_button.disabled:
		_finish()
		return
	warp_button.pressed.emit()
	if not menu._form_fields.has("to_sector_id"):
		_check(false, "warp menu action did not open its destination form")
		_finish()
		return
	(menu._form_fields["to_sector_id"] as LineEdit).text = str(destination)
	menu._inline_submit.pressed.emit()
	var moved := await _wait_until(func() -> bool:
		return (int(app.client_state.snapshot().get("hud", {}).get("sector_id", 0)) == destination
			and not app.gameplay_view.command_menu._busy),
		240
	)
	_check(moved, "Godot warp action did not settle at the selected connected sector")
	var after_move: Dictionary = app.client_state.snapshot()
	_check(int(after_move.get("hud", {}).get("turns_remaining", initial_turns)) == initial_turns - 1, "Godot warp did not consume exactly one turn")
	menu.select_category("NAVIGATION")
	await process_frame
	var scan_button := _find_button(menu._action_list, "Detailed current-sector scan")
	_check(scan_button != null and not scan_button.disabled, "navigation menu did not expose the current-sector scan")
	if scan_button != null and not scan_button.disabled:
		var before_info: String = app.gameplay_view.information_text.text
		scan_button.pressed.emit()
		var scanned := await _wait_until(func() -> bool:
			return (not app.gameplay_view.command_menu._busy
				and app.gameplay_view.information_text.text != before_info),
			180
		)
		_check(scanned, "Godot scan did not return a client-visible server result")
	else:
		_finish()
		return
	print("LIVE_DISPOSABLE_PASS journey=registration,navigation,turns,scan player_id=%d origin=%d destination=%d ship_id=%d" % [int(app.auth_session.player_id), initial_sector, destination, ship_id])
	_finish()

func _wait_until(predicate: Callable, frames: int) -> bool:
	for _frame in frames:
		if predicate.call():
			return true
		await process_frame
	return predicate.call()

func _find_button(parent: Node, label: String) -> Button:
	for child in parent.get_children():
		if child is Button and (child as Button).text == label:
			return child
	return null

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)

func _finish() -> void:
	if is_instance_valid(app):
		app.queue_free()
	await process_frame
	if failures.is_empty():
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)
