extends SceneTree

const MainScene = preload("res://Main.tscn")

class FakeRefresh extends RefCounted:
	var calls: Array = []

	func refresh(_force: bool = false, requested_domains: Array = []) -> bool:
		calls.append(requested_domains.duplicate())
		return true

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	var fake_refresh := FakeRefresh.new()
	app.refresh_coordinator = fake_refresh

	app._pending_game_commands["transwarp-timeout"] = {
		"kind": "command", "command": "move.transwarp", "label": "Transwarp",
	}
	app._on_request_timed_out("transwarp-timeout", "move.transwarp")
	_check(fake_refresh.calls == [["player", "ship", "sector"]], "transwarp timeout did not refresh all hazard-affected domains")
	_check(not app._pending_game_commands.has("transwarp-timeout"), "timed-out transwarp remained pending")
	_check(app.gameplay_view.notification_label.text.contains("Transwarp response timed out"), "transwarp timeout was not presented as uncertain")

	fake_refresh.calls.clear()
	app._pending_game_commands["launch-timeout"] = {
		"kind": "command", "command": "planet.launch", "label": "Launch from planet",
	}
	app._on_request_timed_out("launch-timeout", "planet.launch")
	_check(fake_refresh.calls == [["player", "ship", "sector"]], "launch timeout did not refresh all hazard-affected domains")
	_check(not app._pending_game_commands.has("launch-timeout"), "timed-out launch remained pending")
	_check(app.gameplay_view.notification_label.text.contains("Launch response timed out"), "launch timeout was not presented as uncertain")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 hazard timeout refresh tests: 6 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
