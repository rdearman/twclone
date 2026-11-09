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
	app._route_running = true
	app._route_stop_requested = true
	app._route_stop_after_next = true
	app._planned_route = [42, 43]
	app._route_destination = 43
	app.gameplay_view.show_planned_route(app._planned_route, app._route_destination)

	await app._finish_route_warp({
		"status": "refused",
		"type": "error",
		"error": {"message": "Entry hazard resolved the ship.", "data": {
			"hazards": [{"hazard_type": "minefield", "hull_lost": 100}],
			"ship_destroyed": true,
		}},
	}, {"destination": 42})

	_check(not app._route_running, "terminal hazard refusal left autonav running")
	_check(app._planned_route.is_empty(), "terminal hazard refusal preserved a destroyed route")
	_check(app.gameplay_view.notification_label.text.contains("terminal hazard outcome"), "terminal hazard refusal was presented as an ordinary refusal")
	_check(fake_refresh.calls == [["player", "ship", "sector"]], "terminal hazard refusal did not refresh all affected domains")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 route terminal refusal hazard test: 4 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
