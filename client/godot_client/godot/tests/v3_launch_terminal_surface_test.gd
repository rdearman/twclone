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
		"status": "ok",
		"data": {
			"in_space": false,
			"ship_destroyed": true,
			"hazards": [{"hazard_type": "minefield", "destroyed": true}],
		},
	}, {
		"label": "Launch from planet", "command": "planet.launch", "mutating": true,
	})

	_check(fake_refresh.calls == [["player", "ship", "sector"]], "terminal launch did not refresh all affected domains")
	_check(app._landed_planet_id == 0, "terminal launch retained the confirmed planet identity")
	_check(not app._landing_surface_ready, "terminal launch left the planet surface marked ready")
	_check(not app.gameplay_view.planet_workflow.visible, "terminal launch left the planet surface actionable")
	_check(app.gameplay_view.information_text.text.contains("minefield"), "terminal launch hid the authoritative hazard report")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 terminal launch surface tests: 5 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
