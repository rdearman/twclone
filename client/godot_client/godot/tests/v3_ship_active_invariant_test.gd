extends SceneTree

const MainScene = preload("res://Main.tscn")

class FakeRefresh extends RefCounted:
	var calls: Array = []

	func refresh(_force: bool = false, domains: Array = []) -> bool:
		calls.append(domains.duplicate())
		return true

var failures: Array[String] = []
var checks := 0

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
		"data": {"ships": [{"ship_id": 7, "name": "Stored Hauler", "active": false}]},
	}, {"label": "Owned ship roster", "command": "ship.list", "mutating": false})

	_check(app.gameplay_view.information_text.text.contains("Stored Hauler"), "server-provided inactive ship was hidden by a client invariant")
	_check(refresh.calls.is_empty(), "valid roster triggered an unnecessary refresh")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 active-ship invariant tests: %d checks passed" % checks)
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
