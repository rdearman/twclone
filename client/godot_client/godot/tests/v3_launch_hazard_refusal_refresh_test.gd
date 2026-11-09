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
		"status": "refused",
		"error": {"message": "Launch stopped by entry hazards.", "data": {
			"hazards": [{"hazard_type": "minefield", "hull_lost": 12}],
			"ship_destroyed": false,
		}},
	}, {
		"label": "Launch from planet", "command": "planet.launch", "mutating": true,
	})

	_check(fake_refresh.calls.size() == 1, "hazardous launch refusal did not refresh authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "launch refusal refreshed the wrong state domains")
	_check(app._landed_planet_id == 77, "launch refusal erased the confirmed planet context")
	_check(app.gameplay_view.planet_workflow.visible, "launch refusal closed the confirmed planet workflow")
	_check(app.gameplay_view.information_text.text.contains("minefield"), "launch hazard details were not presented")
	_check(app.gameplay_view.notification_label.text.contains("Launch refused"), "launch refusal was not presented")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 launch hazard refusal refresh test: 6 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
