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
	await app._finish_command_reply({
		"status": "ok", "type": "ship.list.response",
		"data": {"ships": [
			{"ship_id": 7, "name": "Hauler", "active": true},
			{"ship_id": 9, "name": "Scout", "active": "false"},
		]},
	}, {"label": "Owned ship roster", "command": "ship.list", "mutating": false})
	_check(not app.gameplay_view.information_text.text.contains("Scout"), "non-boolean active flag was presented")
	_check(refresh.calls.size() == 1 and refresh.calls[0] == ["player", "ship"], "malformed active flag did not refresh authoritative ship state")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 ship roster active-type tests: 2 checks passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
