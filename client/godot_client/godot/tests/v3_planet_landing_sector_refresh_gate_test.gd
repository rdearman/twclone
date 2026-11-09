extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures: Array[String] = []

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
	app._pending_landing_surface_id = 77
	app._command_refresh_pending = true

	await app._on_refresh_finished({
		"freshness": {"player": "fresh", "ship": "fresh", "sector": "partially available"},
		"player": {"id": 3},
		"ship": {"id": 9, "onplanet": 77},
		"sector": {"id": 17},
	})

	_check(not app._landing_surface_ready, "stale sector refresh opened the planet surface")
	_check(not app.gameplay_view.planet_workflow.visible, "stale sector refresh left planet controls visible")
	_check(app._landed_planet_id == 0, "stale sector refresh retained an unverified landing identity")
	_check(app.gameplay_view.notification_label.text.contains("refresh was incomplete"), "stale sector refresh was not explained")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 planet landing sector-refresh gate test passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
