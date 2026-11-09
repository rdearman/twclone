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
		"type": "planet.land.success",
		"data": {"in_space": true},
	}, {
		"label": "Launch from planet", "command": "planet.launch", "mutating": true,
	})

	# A landing-shaped response must not be treated as a launch merely because
	# it carries the same-shaped in_space marker.
	_check(fake_refresh.calls == [["player", "ship", "sector"]], "mismatched launch response did not refresh affected domains")
	_check(not app._pending_launch_departure, "mismatched launch response marked departure pending")
	_check(app.gameplay_view.notification_label.text.contains("not recognized"), "mismatched launch response was not surfaced")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 launch response type guard tests: 3 checks passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
