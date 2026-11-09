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
		"freshness": {"player": "fresh", "ship": "fresh"},
		"player": {"id": 99},
		"ship": {"id": 9, "onplanet": 77},
	})

	_check(not app._landing_surface_ready, "a different player's fresh state authorized the planet surface")
	_check(not app.gameplay_view.planet_workflow.visible, "a mismatched player refresh exposed planet actions")
	_check(app._pending_landing_surface_id == 0, "mismatched player refresh left a pending landing")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 landing player-identity tests passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
