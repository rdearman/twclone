extends SceneTree

const MainScene = preload("res://Main.tscn")

class FakeRefresh extends RefCounted:
	var calls: Array = []

	func refresh(_force: bool = false, domains: Array = []) -> bool:
		calls.append(domains.duplicate())
		return true

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	var refresh := FakeRefresh.new()
	app.refresh_coordinator = refresh
	app.auth_session.player_id = 11
	app.gameplay_view._snapshot["hud"] = {"ship_id": 7}

	await app._finish_command_reply({
		"status": "ok", "type": "ship.renamed",
		"data": {"ship_id": 7, "name": "Aurora"},
	}, {"label": "Rename ship", "command": "ship.rename", "mutating": true,
		"data": {"name": "Aurora"}})
	_check(app.gameplay_view.notification_label.text.contains("Ship rename confirmed"), "typed rename receipt was not confirmed")
	_check(refresh.calls.size() == 1 and refresh.calls[0] == ["player", "ship"], "confirmed rename did not refresh player and ship state")

	await app._finish_command_reply({
		"status": "ok", "data": {"ship_id": 7, "name": "Forged"},
	}, {"label": "Rename ship", "command": "ship.rename", "mutating": true,
		"data": {"name": "Forged"}})
	_check(app.gameplay_view.notification_label.text.contains("was not confirmed"), "untyped rename receipt was presented as confirmed")
	_check(refresh.calls.size() == 2 and refresh.calls[1] == ["player", "ship"], "ambiguous rename did not refresh player and ship state")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 ship rename contract tests: 4 checks passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
