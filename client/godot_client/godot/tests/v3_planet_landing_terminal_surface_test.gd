extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	app._landed_planet_id = 77
	app._landing_surface_ready = true
	app.gameplay_view.enter_planet_workflow(77)

	await app._finish_command_reply({
		"status": "refused",
		"error": {"message": "Ship destroyed", "data": {"ship_destroyed": true, "ship_remains_in_sector": false}},
	}, {"label": "Land on planet", "command": "planet.land", "mutating": true})

	_check(not app.gameplay_view.planet_workflow.visible, "terminal landing refusal left the surface visible")
	_check(app._landed_planet_id == 0, "terminal landing refusal retained the landed planet")
	_check(not app._landing_surface_ready, "terminal landing refusal retained actionable surface state")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 planet landing terminal surface tests passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
