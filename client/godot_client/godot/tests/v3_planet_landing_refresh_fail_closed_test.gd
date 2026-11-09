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
	app._landed_planet_id = 77
	app._pending_landing_surface_id = 77
	app._command_refresh_pending = true

	await app._on_refresh_finished({
		"freshness": {"player": "fresh", "ship": "fresh"},
		"ship": {"id": 9, "onplanet": 88},
	})

	_check(app._landed_planet_id == 0, "contradictory refresh retained the pending planet identity")
	_check(app._pending_landing_surface_id == 0, "contradictory refresh remained pending")
	_check(not app._landing_surface_ready, "contradictory refresh exposed an actionable surface")
	_check(not app.gameplay_view.planet_workflow.visible, "contradictory refresh left the planet surface visible")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 landing fail-closed tests: %d checks passed" % checks)
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
