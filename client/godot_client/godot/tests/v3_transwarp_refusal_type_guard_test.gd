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

	await app._finish_command_reply(
		{"status": "refused", "type": "planet.info", "error": {
			"message": "Transwarp refused.",
			"data": {"hazards": [{"hazard_type": "untrusted"}]}
		}},
		{"label": "Transwarp", "command": "move.transwarp", "mutating": true, "data": {"to_sector_id": 42}}
	)

	_check(fake_refresh.calls == [["player", "ship", "sector"]], "wrong transwarp refusal type did not refresh authoritative movement state")
	_check(not app.gameplay_view.information_text.text.contains("untrusted"), "wrong transwarp refusal type exposed unrelated hazard data")
	_check(app.gameplay_view.notification_label.text.contains("Transwarp response was not recognized"), "wrong transwarp refusal type was not rejected")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 transwarp refusal-type guard test: 3 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
