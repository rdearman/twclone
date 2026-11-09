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
	app._landing_surface_ready = true
	app.gameplay_view.enter_planet_workflow(77)

	await app._finish_command_reply({
		"status": "refused",
		"error": {"message": "Launch destroyed the ship.", "data": {
			"hazards": [{"hazard_type": "minefield", "hull_lost": 100}],
			"ship_destroyed": true,
		}},
	}, {
		"label": "Launch from planet", "command": "planet.launch", "mutating": true,
	})

	_check(fake_refresh.calls == [["player", "ship", "sector"]], "terminal launch refusal did not refresh all affected domains")
	_check(app._landed_planet_id == 0, "terminal launch refusal retained the confirmed planet ID")
	_check(not app._landing_surface_ready, "terminal launch refusal retained an actionable surface")
	_check(not app.gameplay_view.planet_workflow.visible, "terminal launch refusal left the planet workflow visible")
	_check(app.gameplay_view.notification_label.text.contains("terminal"), "terminal launch refusal was not presented as terminal")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 terminal launch refusal test: 5 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
