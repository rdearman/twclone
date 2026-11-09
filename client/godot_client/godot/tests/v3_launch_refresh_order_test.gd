extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures: Array[String] = []
var checks := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	app.auth_session.handle_response({
		"status": "ok", "type": "auth.session",
		"data": {"session": "test-session", "player_id": 3},
	})
	app._landed_planet_id = 77
	app._landing_surface_ready = true
	app.gameplay_view.enter_planet_workflow(77)
	app._pending_launch_departure = true
	app._command_refresh_pending = true

	await app._on_refresh_finished({
		"freshness": {"player": "fresh", "ship": "fresh", "sector": "partially available"},
		"ship": {"id": 9},
	})
	_check(app._landed_planet_id == 77, "incomplete launch refresh cleared confirmed planet identity")
	_check(app.gameplay_view.planet_workflow.visible, "incomplete launch refresh hid the confirmed planet surface")
	_check(not app._pending_launch_departure, "incomplete launch refresh left a pending transition")

	app._landed_planet_id = 77
	app._landing_surface_ready = true
	app._pending_launch_departure = true
	app._command_refresh_pending = true
	await app._on_refresh_finished({
		"freshness": {"player": "fresh", "ship": "fresh", "sector": "fresh"},
		"ship": {"id": 9, "onplanet": 77},
	})
	_check(app._landed_planet_id == 77, "contradictory launch refresh cleared confirmed planet identity")
	_check(app.gameplay_view.planet_workflow.visible, "contradictory launch refresh hid the confirmed planet surface")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 launch refresh-order tests: %d checks passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
