extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame

	_check(app._planet_landing_terminal({
		"combat": [{"defense": "planetary_fighters", "destroyed": true}]
	}), "terminal planetary combat row was not recognized")
	_check(app._planet_landing_terminal({
		"hazards": [{"hazard_type": "atmosphere_quasar", "ship_remains_in_sector": false}]
	}), "terminal entry hazard row was not recognized")
	_check(not app._planet_landing_terminal({
		"combat": [{"defense": "planetary_fighters", "destroyed": 1, "hull_lost": 100}]
	}), "malformed terminal flag was inferred from non-boolean data")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3-PLAN-01 terminal row test: 3 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
