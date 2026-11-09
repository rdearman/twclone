extends SceneTree

const MainScene = preload("res://Main.tscn")

class FailedRefresh extends RefCounted:
	func refresh(_force: bool = false, _domains: Array = []) -> bool:
		return false

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	app.refresh_coordinator = FailedRefresh.new()
	app.auth_session.player_id = 11
	app.gameplay_view.command_menu.set_owned_ship_roster({
		"ships": [{"ship_id": 7, "active": true}]
	})

	await app._finish_command_reply({
		"status": "refused", "type": "error",
		"error": {"message": "Ship transfer is not available."}
	}, {"label": "Transfer ship", "command": "ship.transfer", "mutating": true,
		"data": {"ship_id": 7, "target_player_id": 42}})

	_check(app.gameplay_view.command_menu._owned_ship_ids.is_empty(), "failed transfer refresh left stale ship IDs actionable")
	_check(app.gameplay_view.notification_label.text.contains("Ship transfer is not available."), "transfer refusal was not surfaced")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 ship transfer refresh failure tests: 2 checks passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
