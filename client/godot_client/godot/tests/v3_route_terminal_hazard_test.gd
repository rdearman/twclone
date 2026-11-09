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
	app._planned_route = [19, 20]

	await app._finish_route_warp({
		"status": "ok",
		"data": {
			"sector_id": 18,
			"hazards": [{"hazard_type": "minefield", "hull_lost": 100}],
			"ship_destroyed": true,
		},
	}, {"destination": 18})

	_check(not app._route_running, "terminal hazard left autonav running")
	_check(app._planned_route.is_empty(), "terminal hazard retained future route hops")
	_check(fake_refresh.calls == [["player", "ship", "sector"]], "terminal hazard did not refresh all authoritative domains")
	_check(app.gameplay_view.notification_label.text.contains("terminal hazard"), "terminal hazard outcome was not presented")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 route terminal hazard test: 4 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
