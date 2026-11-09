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
		{"status": "error", "type": "error", "error": {
			"message": "Ship destroyed by sector hazards during transwarp.",
			"data": {
				"ship_destroyed": true,
				"hazards": [{"hazard_type": "minefield", "hull_lost": 100}]
			}
		}},
		{"label": "Transwarp", "command": "move.transwarp", "mutating": true, "data": {"to_sector_id": 42}}
	)

	_check(app.gameplay_view.notification_label.text.contains("Transwarp outcome is terminal"), "terminal error was presented as a generic refusal")
	_check(app.gameplay_view.information_text.text.contains("minefield"), "terminal error hazard details were not presented")
	_check(fake_refresh.calls == [["player", "ship", "sector"]], "terminal error did not refresh all affected authoritative domains")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 transwarp terminal-error test: 3 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
