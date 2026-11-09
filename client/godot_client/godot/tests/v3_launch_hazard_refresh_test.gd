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
	app._landed_planet_id = 77
	app.gameplay_view.enter_planet_workflow(77)

	await app._finish_command_reply({
		"status": "ok",
		"data": {"message": "Launch processed", "in_space": false},
	}, {
		"label": "Launch from planet", "command": "planet.launch", "mutating": true,
	})

	_check(fake_refresh.calls.size() == 1, "ambiguous launch did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "ambiguous launch refreshed the wrong domains")
	_check(app._landed_planet_id == 77, "ambiguous launch cleared the confirmed planet context")
	_check(app.gameplay_view.planet_workflow.visible, "ambiguous launch closed the planet workflow")
	_check(app.gameplay_view.notification_label.text.contains("outcome is uncertain"), "ambiguous launch was not presented as uncertain")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 launch hazard refresh test: 5 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
