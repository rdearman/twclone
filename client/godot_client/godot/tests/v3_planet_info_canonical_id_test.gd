extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame

	_check(app._response_planet_id({"planet_id": 77}) == 77, "canonical planet_id was not accepted")
	_check(app._response_planet_id({"id": 77}) == 0, "legacy top-level id was accepted as a planet identity")
	_check(app._response_planet_id({"planet": {"planet_id": 77}}) == 0, "nested planet_id alias was accepted")
	_check(app._response_planet_id({"planet_id": true, "id": 77}) == 0, "malformed canonical identity fell through to an alias")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 planet canonical ID test: 4 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
