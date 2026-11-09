extends SceneTree

const MainScene = preload("res://Main.tscn")

class FakeRefresh extends RefCounted:
	var calls: Array = []

	func accepts_request(_request_id: String) -> bool:
		return false

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
	var fake_refresh := FakeRefresh.new()
	app.refresh_coordinator = fake_refresh

	app._pending_game_commands["transwarp-disconnect"] = {
		"kind": "transwarp", "command": "move.transwarp", "label": "Transwarp",
	}
	app._on_request_failed("transwarp-disconnect", "move.transwarp", {
		"kind": "disconnected", "message": "socket closed",
	})
	_check(fake_refresh.calls.size() == 1, "transwarp transport failure did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "transwarp transport failure refreshed the wrong domains")
	_check(not app._pending_game_commands.has("transwarp-disconnect"), "failed transwarp remained pending")
	_check(app.gameplay_view.notification_label.text.contains("Transwarp could not be confirmed"), "transwarp uncertainty was not presented")

	fake_refresh.calls.clear()
	app._pending_game_commands["launch-disconnect"] = {
		"kind": "launch", "command": "planet.launch", "label": "Launch from planet",
	}
	app._on_request_failed("launch-disconnect", "planet.launch", {
		"kind": "disconnected", "message": "socket closed",
	})
	_check(fake_refresh.calls.size() == 1, "launch transport failure did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "launch transport failure refreshed the wrong domains")
	_check(not app._pending_game_commands.has("launch-disconnect"), "failed launch remained pending")
	_check(app.gameplay_view.notification_label.text.contains("Launch could not be confirmed"), "launch uncertainty was not presented")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 transport hazard refresh tests: 8 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
