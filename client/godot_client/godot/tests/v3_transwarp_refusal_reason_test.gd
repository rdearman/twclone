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
	var fake_refresh := FakeRefresh.new()
	app.refresh_coordinator = fake_refresh

	await app._finish_command_reply({
		"status": "refused",
		"type": "error",
		"error": {"message": "Ship does not have transwarp capability."}
	}, {
		"label": "Transwarp",
		"command": "move.transwarp",
		"mutating": true,
		"data": {"to_sector_id": 42}
	})

	_check(app.gameplay_view.notification_label.text.contains("Ship does not have transwarp capability."),
		"canonical transwarp refusal reason was hidden")
	_check(fake_refresh.calls == [["player", "ship", "sector"]],
		"transwarp refusal did not refresh authoritative movement state")

	fake_refresh.calls.clear()
	await app._finish_command_reply({
		"status": "refused",
		"type": "error",
		"error": {"data": {
			"message": "Transwarp interdiction blocked the destination.",
			"hazards": []
		}}
	}, {
		"label": "Transwarp",
		"command": "move.transwarp",
		"mutating": true,
		"data": {"to_sector_id": 42}
	})

	_check(app.gameplay_view.notification_label.text.contains("Transwarp interdiction blocked the destination."),
		"nested canonical transwarp refusal reason was hidden")
	_check(fake_refresh.calls == [["player", "ship", "sector"]],
		"nested transwarp refusal did not refresh authoritative movement state")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 transwarp refusal-reason test: 4 checks passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
