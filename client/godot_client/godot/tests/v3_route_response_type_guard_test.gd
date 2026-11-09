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
		"type": "planet.info",
		"data": {"sector_id": 19, "hazards": []},
	}, {"destination": 19})

	_check(not app._route_running, "mismatched route response left autonav running")
	_check(app._planned_route.is_empty(), "mismatched route response retained future hops")
	_check(fake_refresh.calls == [["player", "ship", "sector"]], "mismatched route response did not refresh authoritative state")
	_check(app.gameplay_view.notification_label.text.contains("response was not recognized"), "mismatched route response was not presented as unrecognized")
	_check(not app.gameplay_view.notification_label.text.contains("Sector 19"), "mismatched route response was presented as confirmed movement")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 route response type guard test: 5 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
