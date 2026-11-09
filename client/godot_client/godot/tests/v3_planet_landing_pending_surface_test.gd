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
	app.refresh_coordinator = FakeRefresh.new()
	app._landed_planet_id = 66
	app._landing_surface_ready = true
	app.gameplay_view.enter_planet_workflow(66)

	await app._finish_command_reply({
		"status": "ok",
		"data": {"planet_id": 77},
	}, {
		"label": "Land on planet",
		"command": "planet.land",
		"data": {"planet_id": 77},
		"mutating": true,
	})

	_check(not app.gameplay_view.planet_workflow.visible, "pending landing left the prior planet surface visible")
	_check(app._landed_planet_id == 77, "pending landing did not retain the server-confirmed target identity")
	_check(not app._landing_surface_ready, "pending landing became surface-ready before refresh")
	_check(app._pending_landing_surface_id == 77, "pending landing identity was not retained for refresh correlation")
	_check(app.refresh_coordinator.calls == [["player", "ship"]], "pending landing refreshed the wrong authoritative domains")

	if failures.is_empty():
		print("Godot V3 planet landing pending-surface test: %d checks passed" % checks)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)
	quit(0)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
